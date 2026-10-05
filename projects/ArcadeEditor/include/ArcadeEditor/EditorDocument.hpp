#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Result.hpp"

class EditorDocument
{
public:
    [[nodiscard]] const std::filesystem::path& Path() const noexcept { return m_Path; }
    [[nodiscard]] const std::string& Text() const noexcept { return m_Text; }
    [[nodiscard]] std::string& MutableText() noexcept { return m_Text; }
    [[nodiscard]] bool IsDirty() const noexcept { return m_Text != m_SavedText; }
    [[nodiscard]] bool HasExternalConflict() const noexcept { return m_ExternalConflict; }

    void SetText(std::string text) { m_Text = std::move(text); }
    void DiscardChanges() { m_Text = m_SavedText; }
    [[nodiscard]] Result<void> ReloadFromDisk();
    [[nodiscard]] Result<void> Save();

private:
    friend class EditorDocumentStore;
    EditorDocument(std::filesystem::path path, std::string text);

    std::filesystem::path m_Path;
    std::string m_Text;
    std::string m_SavedText;
    bool m_ExternalConflict = false;
};

class EditorDocumentStore
{
public:
    [[nodiscard]] Result<std::shared_ptr<EditorDocument>> Open(const std::filesystem::path& path);
    [[nodiscard]] const std::vector<std::shared_ptr<EditorDocument>>& Documents() const noexcept
    {
        return m_Documents;
    }
    [[nodiscard]] Result<void> Close(const std::shared_ptr<EditorDocument>& document);
    [[nodiscard]] Result<void> SaveAll();
    [[nodiscard]] Result<void> ChangePath(const std::filesystem::path& from,
                                          const std::filesystem::path& to);
    [[nodiscard]] Result<void> ReloadExternalChange(const std::filesystem::path& path);
    [[nodiscard]] Result<void> SaveRecovery(const std::filesystem::path& recoveryFile) const;
    [[nodiscard]] Result<std::size_t> RestoreRecovery(const std::filesystem::path& recoveryFile);

private:
    std::unordered_map<std::filesystem::path, std::weak_ptr<EditorDocument>> m_DocumentCache;
    std::vector<std::shared_ptr<EditorDocument>> m_Documents;
};
