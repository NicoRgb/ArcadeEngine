#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Core/Result.hpp"

class EditorDocument;

class EditorCommand
{
public:
    virtual ~EditorCommand() = default;
    [[nodiscard]] virtual const std::string& Name() const noexcept = 0;
    [[nodiscard]] virtual Result<void> Execute() = 0;
    [[nodiscard]] virtual Result<void> Undo() = 0;
    [[nodiscard]] virtual bool References(const EditorDocument*) const noexcept { return false; }
};

class UndoRedoStack
{
public:
    explicit UndoRedoStack(std::size_t capacity = 256);

    [[nodiscard]] Result<void> Execute(std::unique_ptr<EditorCommand> command);
    [[nodiscard]] Result<void> PushApplied(std::unique_ptr<EditorCommand> command);
    [[nodiscard]] Result<void> Undo();
    [[nodiscard]] Result<void> Redo();

    [[nodiscard]] bool CanUndo() const noexcept { return !m_Undo.empty(); }
    [[nodiscard]] bool CanRedo() const noexcept { return !m_Redo.empty(); }
    [[nodiscard]] const std::string& UndoName() const noexcept;
    [[nodiscard]] const std::string& RedoName() const noexcept;
    [[nodiscard]] std::size_t UndoCount() const noexcept { return m_Undo.size(); }
    [[nodiscard]] std::size_t RedoCount() const noexcept { return m_Redo.size(); }
    void ForgetDocument(const EditorDocument* document);
    void Clear() noexcept;

private:
    void PushUndo(std::unique_ptr<EditorCommand> command);

    std::size_t m_Capacity;
    std::vector<std::unique_ptr<EditorCommand>> m_Undo;
    std::vector<std::unique_ptr<EditorCommand>> m_Redo;
};

class TextEditCommand final : public EditorCommand
{
public:
    TextEditCommand(std::shared_ptr<EditorDocument> document, std::string before,
                    std::string after);
    [[nodiscard]] const std::string& Name() const noexcept override { return m_Name; }
    [[nodiscard]] bool References(const EditorDocument* document) const noexcept override
    {
        return m_Document.get() == document;
    }
    [[nodiscard]] Result<void> Execute() override;
    [[nodiscard]] Result<void> Undo() override;

private:
    std::shared_ptr<EditorDocument> m_Document;
    std::string m_Before;
    std::string m_After;
    std::string m_Name = "Edit text";
};

class RenameFileCommand final : public EditorCommand
{
public:
    using PathChanged =
        std::function<Result<void>(const std::filesystem::path&, const std::filesystem::path&)>;

    RenameFileCommand(std::filesystem::path from, std::filesystem::path to,
                      PathChanged pathChanged = {});
    [[nodiscard]] const std::string& Name() const noexcept override { return m_Name; }
    [[nodiscard]] Result<void> Execute() override;
    [[nodiscard]] Result<void> Undo() override;

private:
    [[nodiscard]] Result<void> Rename(const std::filesystem::path& from,
                                      const std::filesystem::path& to);

    std::filesystem::path m_From;
    std::filesystem::path m_To;
    PathChanged m_PathChanged;
    bool m_AtDestination = false;
    std::string m_Name = "Rename file";
};
