# ArcadeEditor UI framework

`ArcadeEditorUI` provides the editor panel registry and built-in panels. Add an
editor panel by deriving from `EditorPanel`, giving it a stable unique `Id()`
and visible `Title()`, implementing `Draw(EditorPanelContext&)`, and registering
a factory with `EditorApplicationFactory::RegisterPanel`. The Panels menu
controls visibility. Default docking uses the visible titles, so retain them
when replacing built-in panels.

Use `EditorPanelContext::OpenFile` from an asset panel to open a document in the
central workspace. It returns the engine `Result` type so normal I/O failures
can be shown without exceptions. Documents are opened once and shown as tabs.
Register a custom `EditorFileEditor` factory with `EditorApplicationFactory`;
unregistered extensions use the plain text editor. Project resources, logs,
selection, and status are shared through the context.

Add an `EditorCommand` implementation for an action that should support undo
and redo. Use `UndoRedoStack::Execute` for commands that have not yet run, or
`PushApplied` when the UI has already changed its model. Commands return engine
`Result` values on failure; failed undo and redo commands remain available for
retry. History is capped at 256 commands by default.

The current presentation path uses ImGui's GLFW and OpenGL3 backends as a
temporary renderer. The editor uses the engine resource manager for decoded
image previews and a multi-sink logger whose bounded console sink receives
engine and editor messages. Projects use versioned `ArcadeProject.json`
manifests with an `assetRoot` relative to the project folder.
