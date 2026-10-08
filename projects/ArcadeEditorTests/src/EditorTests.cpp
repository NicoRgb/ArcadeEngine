#include "ArcadeEditor/EditorAssetWatcher.hpp"
#include "ArcadeEditor/EditorCommand.hpp"
#include "ArcadeEditor/EditorDocument.hpp"
#include "ArcadeEditor/EditorLogBuffer.hpp"
#include "ArcadeEditor/EditorProject.hpp"
#include "ArcadeEditor/EditorThumbnail.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <thread>

namespace
{
class ValueCommand final : public EditorCommand
{
public:
    explicit ValueCommand(int& value) : m_Value(value) {}
    [[nodiscard]] const std::string& Name() const noexcept override { return m_Name; }
    Result<void> Execute() override
    {
        ++m_Value;
        return {};
    }
    Result<void> Undo() override
    {
        --m_Value;
        return {};
    }

private:
    int& m_Value;
    std::string m_Name = "Increment";
};

class FailingCommand final : public EditorCommand
{
public:
    [[nodiscard]] const std::string& Name() const noexcept override { return m_Name; }
    Result<void> Execute() override
    {
        return MAKE_ERROR_MSG(Error::IoFailure, "deliberate failure");
    }
    Result<void> Undo() override { return MAKE_ERROR_MSG(Error::IoFailure, "deliberate failure"); }

private:
    std::string m_Name = "Fail";
};

class TemporaryDirectory
{
public:
    TemporaryDirectory()
        : m_Path(std::filesystem::temp_directory_path() /
                 ("ArcadeEditorTests-" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                  "-" + std::to_string(std::random_device{}())))
    {
        std::filesystem::create_directories(m_Path);
    }
    ~TemporaryDirectory() { std::filesystem::remove_all(m_Path); }
    [[nodiscard]] const std::filesystem::path& Path() const noexcept { return m_Path; }

private:
    std::filesystem::path m_Path;
};
} // namespace

TEST_CASE("Undo redo stack executes commands, branches cleanly, and respects capacity",
          "[editor][history]")
{
    int value = 0;
    UndoRedoStack history(2);
    REQUIRE(history.Execute(std::make_unique<ValueCommand>(value)));
    REQUIRE(history.Execute(std::make_unique<ValueCommand>(value)));
    CHECK(value == 2);
    CHECK(history.UndoCount() == 2);

    REQUIRE(history.Undo());
    CHECK(value == 1);
    REQUIRE(history.Redo());
    CHECK(value == 2);
    REQUIRE(history.Undo());
    REQUIRE(history.Execute(std::make_unique<ValueCommand>(value)));
    CHECK_FALSE(history.CanRedo());
    CHECK(value == 2);
    REQUIRE(history.Execute(std::make_unique<ValueCommand>(value)));
    CHECK(value == 3);
    CHECK(history.UndoCount() == 2);
    REQUIRE(history.Undo());
    REQUIRE(history.Undo());
    CHECK(value == 1);
    CHECK_FALSE(history.CanUndo());
}

TEST_CASE("Text edit commands restore and reapply document contents",
          "[editor][documents][history]")
{
    TemporaryDirectory directory;
    const auto path = directory.Path() / "note.txt";
    {
        std::ofstream output(path);
        output << "before";
    }

    EditorDocumentStore documents;
    auto opened = documents.Open(path);
    REQUIRE(opened);
    auto document = *opened;
    document->SetText("after");
    UndoRedoStack history;
    REQUIRE(history.PushApplied(std::make_unique<TextEditCommand>(document, "before", "after")));
    REQUIRE(history.Undo());
    CHECK(document->Text() == "before");
    CHECK_FALSE(document->IsDirty());
    REQUIRE(history.Redo());
    CHECK(document->Text() == "after");
    CHECK(document->IsDirty());

    history.ForgetDocument(document.get());
    CHECK_FALSE(history.CanUndo());
    CHECK_FALSE(history.CanRedo());
}

TEST_CASE("Document store reuses open files and persists edits", "[editor][documents]")
{
    TemporaryDirectory directory;
    const auto path = directory.Path() / "config.json";
    {
        std::ofstream output(path);
        output << "{\"enabled\":false}";
    }

    EditorDocumentStore documents;
    auto firstResult = documents.Open(path);
    REQUIRE(firstResult);
    auto first = *firstResult;
    auto secondResult = documents.Open(path);
    REQUIRE(secondResult);
    auto second = *secondResult;
    CHECK(first == second);
    CHECK(documents.Documents().size() == 1);
    REQUIRE(documents.Close(first));
    CHECK(documents.Documents().empty());
    firstResult = documents.Open(path);
    REQUIRE(firstResult);
    first = *firstResult;
    CHECK(first == second);
    CHECK(documents.Documents().size() == 1);
    first->SetText("{\"enabled\":true}");
    REQUIRE(first->IsDirty());
    REQUIRE(first->Save());
    CHECK_FALSE(first->IsDirty());
    std::ifstream input(path);
    const std::string saved((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
    CHECK(saved == "{\"enabled\":true}");
}

TEST_CASE("Document store refuses binary data in a text editor", "[editor][documents]")
{
    TemporaryDirectory directory;
    const auto path = directory.Path() / "texture.bin";
    {
        std::ofstream output(path, std::ios::binary);
        const char data[] = {'a', '\0', 'b'};
        output.write(data, sizeof(data));
    }
    EditorDocumentStore documents;
    CHECK_FALSE(documents.Open(path));
    CHECK(documents.Documents().empty());
}

TEST_CASE("Document save failures preserve edits and external changes are reported",
          "[editor][documents]")
{
    TemporaryDirectory directory;
    const auto path = directory.Path() / "scene.json";
    {
        std::ofstream output(path);
        output << "original";
    }
    EditorDocumentStore documents;
    auto opened = documents.Open(path);
    REQUIRE(opened);
    auto document = *opened;
    document->SetText("unsaved");

    REQUIRE(std::filesystem::remove(path));
    REQUIRE(std::filesystem::create_directory(path));
    CHECK_FALSE(document->Save());
    CHECK(document->IsDirty());
    CHECK(document->Text() == "unsaved");

    REQUIRE(std::filesystem::remove(path));
    {
        std::ofstream output(path);
        output << "external";
    }
    CHECK_FALSE(documents.ReloadExternalChange(path));
    CHECK(document->HasExternalConflict());
    CHECK(document->Text() == "unsaved");

    document->DiscardChanges();
    REQUIRE(documents.ReloadExternalChange(path));
    CHECK(document->Text() == "external");
    CHECK_FALSE(document->HasExternalConflict());
}

TEST_CASE("File rename commands support undo and redo", "[editor][history][files]")
{
    TemporaryDirectory directory;
    const auto source = directory.Path() / "source.txt";
    const auto target = directory.Path() / "target.txt";
    {
        std::ofstream output(source);
        output << "content";
    }
    UndoRedoStack history;
    REQUIRE(history.Execute(std::make_unique<RenameFileCommand>(source, target)));
    CHECK_FALSE(std::filesystem::exists(source));
    CHECK(std::filesystem::exists(target));
    REQUIRE(history.Undo());
    CHECK(std::filesystem::exists(source));
    CHECK_FALSE(std::filesystem::exists(target));
    REQUIRE(history.Redo());
    CHECK(std::filesystem::exists(target));
}

TEST_CASE("Renaming an open document updates its path through undo and redo",
          "[editor][documents][files]")
{
    TemporaryDirectory directory;
    const auto source = directory.Path() / "source.json";
    const auto target = directory.Path() / "renamed.json";
    {
        std::ofstream output(source);
        output << "{}";
    }
    EditorDocumentStore documents;
    auto opened = documents.Open(source);
    REQUIRE(opened);
    auto document = *opened;
    UndoRedoStack history;
    REQUIRE(history.Execute(std::make_unique<RenameFileCommand>(
        source, target, [&documents](const auto& from, const auto& to)
        { return documents.ChangePath(from, to); })));
    CHECK(document->Path() == std::filesystem::weakly_canonical(target));
    REQUIRE(documents.Close(document));
    auto targetOpened = documents.Open(target);
    REQUIRE(targetOpened);
    CHECK(*targetOpened == document);
    REQUIRE(documents.Close(document));
    REQUIRE(history.Undo());
    CHECK(document->Path() == std::filesystem::weakly_canonical(source));
    auto sourceOpened = documents.Open(source);
    REQUIRE(sourceOpened);
    CHECK(*sourceOpened == document);
    REQUIRE(documents.Close(document));
    REQUIRE(history.Redo());
    CHECK(document->Path() == std::filesystem::weakly_canonical(target));
}

TEST_CASE("File rename commands refuse to replace another file", "[editor][history][files]")
{
    TemporaryDirectory directory;
    const auto source = directory.Path() / "source.txt";
    const auto target = directory.Path() / "target.txt";
    {
        std::ofstream output(source);
        output << "source";
    }
    {
        std::ofstream output(target);
        output << "keep";
    }

    UndoRedoStack history;
    CHECK_FALSE(history.Execute(std::make_unique<RenameFileCommand>(source, target)));
    CHECK(std::filesystem::exists(source));
    std::ifstream input(target);
    const std::string contents((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    CHECK(contents == "keep");
    CHECK_FALSE(history.CanUndo());
}

TEST_CASE("Failed commands stay out of undo history and preserve redo history", "[editor][history]")
{
    int value = 0;
    UndoRedoStack history;
    REQUIRE(history.Execute(std::make_unique<ValueCommand>(value)));
    REQUIRE(history.Undo());
    CHECK(history.CanRedo());
    auto failed = history.Execute(std::make_unique<FailingCommand>());
    CHECK_FALSE(failed);
    CHECK(failed.error().Code == Error::IoFailure);
    CHECK(history.CanRedo());
    CHECK_FALSE(history.CanUndo());
    REQUIRE(history.Redo());
    CHECK(value == 1);
}

TEST_CASE("Projects create and validate a versioned manifest", "[editor][projects]")
{
    TemporaryDirectory directory;
    const auto root = directory.Path() / "Sample";
    auto created = EditorProject::Create(root, "Sample Project");
    REQUIRE(created);
    CHECK(created->AssetRoot == root / "Assets");
    CHECK(std::filesystem::exists(root / "ArcadeProject.json"));
    auto opened = EditorProject::Open(root / "ArcadeProject.json");
    REQUIRE(opened);
    CHECK(opened->Name == "Sample Project");
    CHECK(opened->Root == created->Root);
    CHECK(opened->Settings.is_object());
    CHECK_FALSE(EditorProject::Create(root, "duplicate"));

    const auto outside = directory.Path() / "outside";
    std::filesystem::create_directories(outside);
    std::ofstream invalidManifest(root / "ArcadeProject.json", std::ios::trunc);
    invalidManifest << R"({"version":1,"name":"Unsafe","assetRoot":"../outside"})";
    invalidManifest.close();
    CHECK_FALSE(EditorProject::Open(root / "ArcadeProject.json"));
}

TEST_CASE("Thumbnail cache decodes and releases image resources", "[editor][assets][resources]")
{
    TemporaryDirectory directory;
    const auto imagePath = directory.Path() / "pixel.ppm";
    {
        std::ofstream output(imagePath, std::ios::binary);
        output << "P6\n1 1\n255\n";
        const char pixel[] = {static_cast<char>(255), 0, 0};
        output.write(pixel, sizeof(pixel));
    }

    ResourceManager resources;
    EditorThumbnailCache thumbnails;
    auto image = thumbnails.Get(resources, imagePath);
    REQUIRE(image);
    CHECK((*image)->Width == 1);
    CHECK((*image)->Height == 1);
    CHECK((*image)->Rgba.size() == 4);
    const auto resourceId = image->Id();
    thumbnails.Invalidate(resources, imagePath);
    CHECK_FALSE(resources.Contains(resourceId));
}

TEST_CASE("Project service maintains a bounded recent project list", "[editor][projects]")
{
    TemporaryDirectory directory;
    EditorProjectService projects(directory.Path() / "settings" / "recent.json");
    auto created = projects.Create(directory.Path() / "One", "One");
    REQUIRE(created);
    auto recent = projects.RecentProjects();
    REQUIRE(recent);
    REQUIRE(recent->size() == 1);
    CHECK(recent->front() == created->DescriptorPath());
}

TEST_CASE("Editor log buffer stores bounded, thread-safe log records", "[editor][logger]")
{
    EditorLogBuffer logs(2);
    logs.ReceiveMessage(LogLevel::Info, "one");
    logs.ReceiveMessage(LogLevel::Warn, "two");
    logs.ReceiveMessage(LogLevel::Error, "three");
    auto entries = logs.Snapshot();
    REQUIRE(entries.size() == 2);
    CHECK(entries.front().Message == "two");
    CHECK(entries.back().Level == LogLevel::Error);
    logs.Clear();
    CHECK(logs.Snapshot().empty());
}

TEST_CASE("Asset watcher delivers coalesced filesystem changes", "[editor][assets][watcher]")
{
    TemporaryDirectory directory;
    EditorAssetWatcher watcher;
    REQUIRE(watcher.Start(directory.Path()));
    const auto changedFile = directory.Path() / "watched.json";
    for (int i = 0; i < 4; ++i)
    {
        std::ofstream output(changedFile,
                             std::ios::binary | (i == 0 ? std::ios::trunc : std::ios::app));
        output << "{}";
        output.flush();
    }
    std::vector<std::filesystem::path> changes;
    for (int attempt = 0; attempt < 30 && changes.empty(); ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        changes = watcher.DrainChanges();
    }
    CHECK(std::ranges::find(changes, std::filesystem::weakly_canonical(changedFile)) !=
          changes.end());
    watcher.Stop();
    CHECK_FALSE(watcher.IsWatching());
}
