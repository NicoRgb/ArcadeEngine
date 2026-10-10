#include "Assets/ShaderAsset.hpp"
#include "Application/Application.hpp"

#include <fstream>
#include <iterator>
#include <vector>

#include <slang-com-ptr.h>
#include <slang.h>

REGISTER_ASSET_TYPE(ShaderAsset, ".slang", ".hlsl", ".glsl", ".shader", ".vert", ".frag", ".comp")

namespace
{
struct ShaderSubsystem
{
    Slang::ComPtr<slang::IGlobalSession> GlobalSession;
    SlangResult InitializationResult = SLANG_FAIL;

    ShaderSubsystem()
    {
        InitializationResult = slang::createGlobalSession(GlobalSession.writeRef());
    }
};

ShaderSubsystem& GetShaderSubsystem()
{
    static ShaderSubsystem subsystem;
    return subsystem;
}

std::string GetDiagnostics(slang::IBlob* diagnostics)
{
    if (!diagnostics || !diagnostics->getBufferPointer() || diagnostics->getBufferSize() == 0)
    {
        return {};
    }
    return {static_cast<const char*>(diagnostics->getBufferPointer()),
            diagnostics->getBufferSize()};
}

ShaderParameter CopyParameterReflection(slang::VariableLayoutReflection* parameter)
{
    ShaderParameter result;
    if (const char* name = parameter->getName())
    {
        result.Name = name;
    }
    if (auto* type = parameter->getType(); type && type->getName())
    {
        result.TypeName = type->getName();
    }
    result.BindingIndex = parameter->getBindingIndex();
    result.BindingSpace = parameter->getBindingSpace();
    return result;
}

Result<Slang::ComPtr<slang::ISession>> CreateSession(slang::IGlobalSession* globalSession,
                                                     const std::filesystem::path& includeDirectory)
{
    slang::TargetDesc target = {};
#if defined(_WIN32)
    target.format = SLANG_DXIL;
    target.profile = globalSession->findProfile("sm_6_6");
#else
    target.format = SLANG_SPIRV;
    target.profile = globalSession->findProfile("spirv_1_5");
#endif
    if (target.profile == SLANG_PROFILE_UNKNOWN)
    {
        return MAKE_ERROR_MSG(Error::Unsupported,
                              "Slang could not find the requested shader profile.");
    }

    const std::string searchPath = includeDirectory.string();
    const char* searchPaths[] = {searchPath.c_str()};
    slang::SessionDesc desc = {};
    desc.targets = &target;
    desc.targetCount = 1;
    desc.searchPaths = searchPaths;
    desc.searchPathCount = 1;

    Slang::ComPtr<slang::ISession> session;
    const SlangResult result = globalSession->createSession(desc, session.writeRef());
    if (SLANG_FAILED(result))
    {
        return MAKE_ERROR_MSG(Error::Internal, "Slang could not create a compilation session.");
    }
    return session;
}
} // namespace

namespace
{
ShaderStage ConvertStage(SlangStage stage)
{
    switch (stage)
    {
    case SLANG_STAGE_VERTEX:
        return ShaderStage::Vertex;
    case SLANG_STAGE_HULL:
        return ShaderStage::Hull;
    case SLANG_STAGE_DOMAIN:
        return ShaderStage::Domain;
    case SLANG_STAGE_GEOMETRY:
        return ShaderStage::Geometry;
    case SLANG_STAGE_FRAGMENT:
        return ShaderStage::Fragment;
    case SLANG_STAGE_COMPUTE:
        return ShaderStage::Compute;
    case SLANG_STAGE_RAY_GENERATION:
        return ShaderStage::RayGeneration;
    case SLANG_STAGE_INTERSECTION:
        return ShaderStage::Intersection;
    case SLANG_STAGE_ANY_HIT:
        return ShaderStage::AnyHit;
    case SLANG_STAGE_CLOSEST_HIT:
        return ShaderStage::ClosestHit;
    case SLANG_STAGE_MISS:
        return ShaderStage::Miss;
    case SLANG_STAGE_CALLABLE:
        return ShaderStage::Callable;
    case SLANG_STAGE_MESH:
        return ShaderStage::Mesh;
    case SLANG_STAGE_AMPLIFICATION:
        return ShaderStage::Amplification;
    default:
        return ShaderStage::Unknown;
    }
}
} // namespace

ShaderAsset::~ShaderAsset() = default;

const std::vector<ShaderEntryPoint>& ShaderAsset::GetEntryPoints() const noexcept
{
    static const std::vector<ShaderEntryPoint> noEntryPoints;
    return m_CompiledData ? m_CompiledData->EntryPoints : noEntryPoints;
}

const std::vector<ShaderParameter>& ShaderAsset::GetGlobalParameters() const noexcept
{
    static const std::vector<ShaderParameter> noParameters;
    return m_CompiledData ? m_CompiledData->GlobalParameters : noParameters;
}

Result<void> ShaderAsset::Load()
{
    LOG_INFO("Reloading ShaderAsset {}", m_Path.filename().string());

    std::ifstream input(m_Path, std::ios::binary);
    if (!input)
    {
        return MAKE_ERROR_MSG(Error::NotFound, "Could not open shader: " + m_Path.string());
    }
    std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (input.bad())
    {
        return MAKE_ERROR_MSG(Error::IoFailure, "Could not read shader: " + m_Path.string());
    }
    if (m_CompiledData && m_CompiledData->IsFresh(source))
    {
        return {};
    }

    auto& subsystem = GetShaderSubsystem();
    if (SLANG_FAILED(subsystem.InitializationResult) || !subsystem.GlobalSession)
    {
        return MAKE_ERROR_MSG(Error::Internal, "Could not initialize the Slang compiler session.");
    }

    auto sessionResult = CreateSession(subsystem.GlobalSession, m_Path.parent_path());
    if (!sessionResult)
    {
        return FORWARD_ERROR(sessionResult);
    }
    auto session = std::move(*sessionResult);

    Slang::ComPtr<slang::IBlob> diagnostics;
    Slang::ComPtr<slang::IModule> module;
    const std::string moduleName = m_Path.stem().string();
    const std::string sourcePath = m_Path.string();
    module.attach(session->loadModuleFromSourceString(moduleName.c_str(), sourcePath.c_str(),
                                                      source.c_str(), diagnostics.writeRef()));
    const std::string moduleDiagnostics = GetDiagnostics(diagnostics);
    if (!module)
    {
        return MAKE_ERROR_MSG(Error::ParseFailure,
                              "Slang failed to compile " + sourcePath +
                                  (moduleDiagnostics.empty() ? "." : ":\n" + moduleDiagnostics));
    }

    std::vector<Slang::ComPtr<slang::IEntryPoint>> entryPointComponents;
    const SlangInt32 entryPointCount = module->getDefinedEntryPointCount();
    entryPointComponents.reserve(static_cast<size_t>(entryPointCount));
    for (SlangInt32 index = 0; index < entryPointCount; ++index)
    {
        Slang::ComPtr<slang::IEntryPoint> entryPoint;
        const SlangResult result = module->getDefinedEntryPoint(index, entryPoint.writeRef());
        if (SLANG_FAILED(result))
        {
            return MAKE_ERROR_MSG(Error::Internal,
                                  "Slang could not retrieve a shader entry point.");
        }
        entryPointComponents.push_back(std::move(entryPoint));
    }
    if (entryPointComponents.empty())
    {
        constexpr std::pair<const char*, SlangStage> conventionalMainStages[] = {
            {"main", SLANG_STAGE_FRAGMENT},
            {"main", SLANG_STAGE_VERTEX},
            {"main", SLANG_STAGE_COMPUTE},
        };
        for (const auto& [name, stage] : conventionalMainStages)
        {
            Slang::ComPtr<slang::IEntryPoint> entryPoint;
            diagnostics.setNull();
            const SlangResult result = module->findAndCheckEntryPoint(
                name, stage, entryPoint.writeRef(), diagnostics.writeRef());
            if (SLANG_SUCCEEDED(result))
            {
                entryPointComponents.push_back(std::move(entryPoint));
                break;
            }
        }
    }
    if (entryPointComponents.empty())
    {
        return MAKE_ERROR_MSG(
            Error::ParseFailure,
            "Shader has no entry points. Mark shader functions with [shader(\"vertex\")], "
            "[shader(\"fragment\")], or another Slang shader-stage attribute.");
    }

    std::vector<slang::IComponentType*> components;
    components.reserve(entryPointComponents.size() + 1);
    components.push_back(module.get());
    auto compiledData = std::make_unique<CompiledData>();
    compiledData->Source = source;
    compiledData->EntryPoints.reserve(entryPointComponents.size());
    for (SlangInt32 index = 0; index < module->getDependencyFileCount(); ++index)
    {
        const char* dependencyPath = module->getDependencyFilePath(index);
        if (!dependencyPath)
        {
            continue;
        }
        std::error_code error;
        const auto modifiedTime = std::filesystem::last_write_time(dependencyPath, error);
        if (!error)
        {
            compiledData->Dependencies.push_back({dependencyPath, modifiedTime});
        }
    }
    std::error_code sourceTimeError;
    const auto sourceModifiedTime = std::filesystem::last_write_time(m_Path, sourceTimeError);
    if (!sourceTimeError)
    {
        compiledData->Dependencies.push_back({m_Path, sourceModifiedTime});
    }
    for (const auto& entryPoint : entryPointComponents)
    {
        components.push_back(entryPoint.get());
        auto* function = entryPoint->getFunctionReflection();
        compiledData->EntryPoints.push_back({function->getName(), ShaderStage::Unknown, {}});
    }

    Slang::ComPtr<slang::IComponentType> composedProgram;
    diagnostics.setNull();
    SlangResult result = session->createCompositeComponentType(
        components.data(), static_cast<SlangInt>(components.size()), composedProgram.writeRef(),
        diagnostics.writeRef());
    std::string compileDiagnostics = GetDiagnostics(diagnostics);
    if (SLANG_FAILED(result))
    {
        return MAKE_ERROR_MSG(Error::ParseFailure,
                              "Slang failed to link " + sourcePath +
                                  (compileDiagnostics.empty() ? "." : ":\n" + compileDiagnostics));
    }

    Slang::ComPtr<slang::IComponentType> program;
    diagnostics.setNull();
    result = composedProgram->link(program.writeRef(), diagnostics.writeRef());
    compileDiagnostics = GetDiagnostics(diagnostics);
    if (SLANG_FAILED(result))
    {
        return MAKE_ERROR_MSG(Error::ParseFailure,
                              "Slang failed to link " + sourcePath +
                                  (compileDiagnostics.empty() ? "." : ":\n" + compileDiagnostics));
    }

    diagnostics.setNull();
    slang::ProgramLayout* programLayout = program->getLayout(0, diagnostics.writeRef());
    compileDiagnostics = GetDiagnostics(diagnostics);
    if (!programLayout)
    {
        return MAKE_ERROR_MSG(Error::ParseFailure,
                              "Slang failed to reflect " + sourcePath +
                                  (compileDiagnostics.empty() ? "." : ":\n" + compileDiagnostics));
    }
    for (size_t index = 0; index < compiledData->EntryPoints.size(); ++index)
    {
        auto* reflectedEntryPoint =
            programLayout->getEntryPointByIndex(static_cast<SlangUInt>(index));
        auto& entryPoint = compiledData->EntryPoints[index];
        entryPoint.Stage = ConvertStage(reflectedEntryPoint->getStage());
        for (unsigned parameterIndex = 0; parameterIndex < reflectedEntryPoint->getParameterCount();
             ++parameterIndex)
        {
            entryPoint.Parameters.push_back(
                CopyParameterReflection(reflectedEntryPoint->getParameterByIndex(parameterIndex)));
        }
    }
    for (unsigned parameterIndex = 0; parameterIndex < programLayout->getParameterCount();
         ++parameterIndex)
    {
        compiledData->GlobalParameters.push_back(
            CopyParameterReflection(programLayout->getParameterByIndex(parameterIndex)));
    }

    for (size_t index = 0; index < compiledData->EntryPoints.size(); ++index)
    {
        diagnostics.setNull();
        Slang::ComPtr<slang::IBlob> code;
        result = program->getEntryPointCode(static_cast<SlangInt>(index), 0, code.writeRef(),
                                            diagnostics.writeRef());
        compileDiagnostics = GetDiagnostics(diagnostics);
        if (SLANG_FAILED(result))
        {
            return MAKE_ERROR_MSG(
                Error::ParseFailure,
                "Slang failed to generate code for entry point " +
                    compiledData->EntryPoints[index].Name +
                    (compileDiagnostics.empty() ? "." : ":\n" + compileDiagnostics));
        }
        const auto* bytes = static_cast<const std::byte*>(code->getBufferPointer());
        const size_t byteCount = code->getBufferSize();
        compiledData->EntryPoints[index].Code.assign(bytes, bytes + byteCount);
    }

    m_Source = std::move(source);
    m_CompiledData = std::move(compiledData);
    return {};
}
