/**************************************************************************/
/*  mesh_splitter_editor_plugin.h                                         */
/**************************************************************************/

#pragma once

#include "../mesh_split_settings.h"

#include "editor/plugins/editor_plugin.h"

class AcceptDialog;
class ConfirmationDialog;
class EditorFileDialog;
class EditorInspector;
class MeshInstance3D;

class MeshSplitterEditor : public Control {
	GDCLASS(MeshSplitterEditor, Control);

	enum {
		MENU_OPTION_SPLIT_MESH = 10000,
	};

	MeshInstance3D *node = nullptr;
	ConfirmationDialog *dialog = nullptr;
	EditorInspector *inspector = nullptr;
	AcceptDialog *error_dialog = nullptr;
	EditorFileDialog *load_settings_dialog = nullptr;
	EditorFileDialog *save_settings_dialog = nullptr;
	Ref<MeshSplitSettings> settings;

	void _menu_option(int p_option);
	void _open_dialog();
	void _popup_load_settings();
	void _popup_save_settings();
	void _load_settings(const String &p_path);
	void _save_settings(const String &p_path);
	void _create_parts();
	void _node_removed(Node *p_node);

public:
	void edit(MeshInstance3D *p_node);
	void set_visible_for_editor(bool p_visible);
	MeshSplitterEditor();
};

class EditorPluginMeshSplitter : public EditorPlugin {
	GDCLASS(EditorPluginMeshSplitter, EditorPlugin);

	MeshSplitterEditor *splitter_editor = nullptr;

public:
	virtual String get_plugin_name() const override { return "MeshSplitter"; }
	virtual void edit(Object *p_object) override;
	virtual bool handles(Object *p_object) const override;
	virtual void make_visible(bool p_visible) override;

	EditorPluginMeshSplitter();
};
