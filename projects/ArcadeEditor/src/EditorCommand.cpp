#include "ArcadeEditor/EditorCommand.hpp"

#include "ArcadeEditor/EditorDocument.hpp"

#include <algorithm>
#include <system_error>

UndoRedoStack::UndoRedoStack(std::size_t capacity) : m_Capacity(capacity)
{
    m_Undo.reserve(capacity);
    m_Redo.reserve(capacity);
}

Result<void> UndoRedoStack::Execute(std::unique_ptr<EditorCommand> command)
{
    if (!command)
    {
        return MakeError(Error::InvalidArgument, "Cannot execute an empty editor command.");
    }
    auto result = command->Execute();
    if (!result)
    {
        return MakeError(result.error());
    }
    PushUndo(std::move(command));
    m_Redo.clear();
    return {};
}

Result<void> UndoRedoStack::PushApplied(std::unique_ptr<EditorCommand> command)
{
    if (!command)
    {
        return MakeError(Error::InvalidArgument, "Cannot record an empty editor command.");
    }
    PushUndo(std::move(command));
    m_Redo.clear();
    return {};
}

void UndoRedoStack::PushUndo(std::unique_ptr<EditorCommand> command)
{
    if (m_Capacity == 0)
    {
        return;
    }
    if (m_Undo.size() == m_Capacity)
    {
        m_Undo.erase(m_Undo.begin());
    }
    m_Undo.push_back(std::move(command));
}

Result<void> UndoRedoStack::Undo()
{
    if (m_Undo.empty())
    {
        return MakeError(Error::InvalidState, "There is no command to undo.");
    }

    auto& command = m_Undo.back();
    auto result = command->Undo();
    if (!result)
    {
        return MakeError(result.error());
    }
    m_Redo.push_back(std::move(command));
    m_Undo.pop_back();
    return {};
}

Result<void> UndoRedoStack::Redo()
{
    if (m_Redo.empty())
    {
        return MakeError(Error::InvalidState, "There is no command to redo.");
    }

    auto& command = m_Redo.back();
    auto result = command->Execute();
    if (!result)
    {
        return MakeError(result.error());
    }
    m_Undo.push_back(std::move(command));
    m_Redo.pop_back();
    return {};
}

const std::string& UndoRedoStack::UndoName() const noexcept
{
    static const std::string empty;
    return m_Undo.empty() ? empty : m_Undo.back()->Name();
}

const std::string& UndoRedoStack::RedoName() const noexcept
{
    static const std::string empty;
    return m_Redo.empty() ? empty : m_Redo.back()->Name();
}

void UndoRedoStack::Clear() noexcept
{
    m_Undo.clear();
    m_Redo.clear();
}

void UndoRedoStack::ForgetDocument(const EditorDocument* document)
{
    const auto isForDocument = [document](const auto& command)
    { return command->References(document); };
    std::erase_if(m_Undo, isForDocument);
    std::erase_if(m_Redo, isForDocument);
}

TextEditCommand::TextEditCommand(std::shared_ptr<EditorDocument> document, std::string before,
                                 std::string after)
    : m_Document(std::move(document)), m_Before(std::move(before)), m_After(std::move(after))
{
}

Result<void> TextEditCommand::Execute()
{
    if (!m_Document)
    {
        return MakeError(Error::InvalidArgument, "Text edit command has no document.");
    }
    m_Document->SetText(m_After);
    return {};
}

Result<void> TextEditCommand::Undo()
{
    if (!m_Document)
    {
        return MakeError(Error::InvalidArgument, "Text edit command has no document.");
    }
    m_Document->SetText(m_Before);
    return {};
}

RenameFileCommand::RenameFileCommand(std::filesystem::path from, std::filesystem::path to,
                                     PathChanged pathChanged)
    : m_From(std::move(from)), m_To(std::move(to)), m_PathChanged(std::move(pathChanged))
{
}

Result<void> RenameFileCommand::Rename(const std::filesystem::path& from,
                                       const std::filesystem::path& to)
{
    if (from.empty() || to.empty() || from == to)
    {
        return MakeError(Error::InvalidArgument, "A file rename requires two different paths.");
    }
    std::error_code error;
    const bool destinationExists = std::filesystem::exists(to, error);
    if (error)
    {
        return MakeError(Error::IoFailure, "Unable to inspect rename destination: " + to.string(),
                         error);
    }
    if (destinationExists)
    {
        return MakeError(Error::AlreadyExists,
                         "Refusing to overwrite an existing file: " + to.string());
    }
    std::filesystem::rename(from, to, error);
    if (error)
    {
        return MakeError(Error::IoFailure, "Unable to rename file: " + from.string(), error);
    }
    if (m_PathChanged)
    {
        auto changed = m_PathChanged(from, to);
        if (!changed)
        {
            std::error_code rollbackError;
            std::filesystem::rename(to, from, rollbackError);
            if (rollbackError)
            {
                return MakeError(Error::IoFailure,
                                 "Rename metadata update failed and file rollback also failed.",
                                 rollbackError);
            }
            return MakeError(changed.error());
        }
    }
    return {};
}

Result<void> RenameFileCommand::Execute()
{
    if (m_AtDestination)
    {
        return {};
    }
    auto result = Rename(m_From, m_To);
    if (result)
    {
        m_AtDestination = true;
    }
    return result;
}

Result<void> RenameFileCommand::Undo()
{
    if (!m_AtDestination)
    {
        return {};
    }
    auto result = Rename(m_To, m_From);
    if (result)
    {
        m_AtDestination = false;
    }
    return result;
}
