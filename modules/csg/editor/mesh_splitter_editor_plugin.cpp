/**************************************************************************/
/*  mesh_splitter_editor_plugin.cpp                                       */
/**************************************************************************/

#include "mesh_splitter_editor_plugin.h"

#include "../mesh_splitter.h"

#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/callable_mp.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/scene/3d/mesh_instance_3d_editor_plugin.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "editor/themes/editor_scale.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/physics/rigid_body_3d.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/label.h"
#include "scene/gui/popup_menu.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/3d/convex_polygon_shape_3d.h"

static void _mesh_split_add_owner_undo(EditorUndoRedoManager *p_undo_redo, Node *p_node, Node *p_owner) {
	p_undo_redo->add_do_method(p_node, "set_owner", p_owner);
	if (Node3D *node_3d = Object::cast_to<Node3D>(p_node)) {
		p_undo_redo->add_do_method(Node3DEditor::get_singleton(), SceneStringName(_request_gizmo), node_3d);
	}
	for (int child_i = 0; child_i < p_node->get_child_count(); child_i++) {
		_mesh_split_add_owner_undo(p_undo_redo, p_node->get_child(child_i), p_owner);
	}
}

static void _mesh_split_copy_material_overrides(MeshInstance3D *p_source, MeshInstance3D *p_target) {
	p_target->set_material_override(p_source->get_material_override());
	const int surface_count = MIN(p_source->get_surface_override_material_count(), p_target->get_mesh()->get_surface_count());
	for (int surface = 0; surface < surface_count; surface++) {
		p_target->set_surface_override_material(surface, p_source->get_surface_override_material(surface));
	}
}

void MeshSplitterEditor::_node_removed(Node *p_node) {
	if (p_node == node) {
		dialog->hide();
		inspector->edit(nullptr);
		node = nullptr;
	}
}

void MeshSplitterEditor::edit(MeshInstance3D *p_node) {
	if (node != p_node) {
		if (dialog->is_visible()) {
			dialog->hide();
		}
		inspector->edit(nullptr);
	}
	node = p_node;
}

void MeshSplitterEditor::set_visible_for_editor(bool p_visible) {
	if (!p_visible) {
		dialog->hide();
	}
}

void MeshSplitterEditor::_menu_option(int p_option) {
	if (p_option == MENU_OPTION_SPLIT_MESH && node) {
		_open_dialog();
	}
}

void MeshSplitterEditor::_open_dialog() {
	ERR_FAIL_NULL(node);
	Ref<Mesh> mesh = node->get_mesh();
	if (mesh.is_null()) {
		error_dialog->set_text(TTR("MeshInstance3D has no mesh to split."));
		error_dialog->popup_centered();
		return;
	}
	// Import settings written by the initial implementation once, but never
	// write them back to node metadata. They become the editor's last-used
	// settings and can be saved explicitly as a resource.
	if (node->has_meta(SNAME("mesh_split_settings"))) {
		const Variant legacy_metadata = node->get_meta(SNAME("mesh_split_settings"));
		if (settings.is_null()) {
			Ref<MeshSplitSettings> legacy_settings = legacy_metadata;
			if (legacy_settings.is_valid()) {
				Ref<Resource> duplicated_settings = legacy_settings->duplicate(true);
				settings = duplicated_settings;
			}
		}
		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		undo_redo->create_action(TTR("Remove Legacy Mesh Split Settings Metadata"));
		undo_redo->add_do_method(node, "remove_meta", SNAME("mesh_split_settings"));
		undo_redo->add_undo_method(node, "set_meta", SNAME("mesh_split_settings"), legacy_metadata);
		undo_redo->commit_action();
	}
	if (settings.is_null()) {
		settings.instantiate();
	}
	inspector->edit(settings.ptr());
	dialog->popup_centered_clamped(Size2(560, 680) * EDSCALE, 0.8);
}

void MeshSplitterEditor::_popup_load_settings() {
	load_settings_dialog->popup_file_dialog();
}

void MeshSplitterEditor::_popup_save_settings() {
	if (settings.is_null()) {
		settings.instantiate();
	}
	if (settings->get_path().is_resource_file()) {
		save_settings_dialog->set_current_path(settings->get_path());
	} else {
		save_settings_dialog->set_current_file("mesh_split_settings.tres");
	}
	save_settings_dialog->popup_file_dialog();
}

void MeshSplitterEditor::_load_settings(const String &p_path) {
	Ref<MeshSplitSettings> loaded = ResourceLoader::load(p_path, "MeshSplitSettings");
	if (loaded.is_null()) {
		error_dialog->set_text(vformat(TTR("Could not load MeshSplitSettings from:\n%s"), p_path));
		error_dialog->popup_centered();
		return;
	}
	settings = loaded;
	inspector->edit(settings.ptr());
}

void MeshSplitterEditor::_save_settings(const String &p_path) {
	ERR_FAIL_COND(settings.is_null());
	const Error error = ResourceSaver::save(settings, p_path, ResourceSaver::FLAG_CHANGE_PATH);
	if (error != OK) {
		error_dialog->set_text(vformat(TTR("Could not save MeshSplitSettings to:\n%s"), p_path));
		error_dialog->popup_centered();
	}
}

void MeshSplitterEditor::_create_parts() {
	ERR_FAIL_NULL(node);
	ERR_FAIL_COND(settings.is_null());
	if (node == get_tree()->get_edited_scene_root()) {
		error_dialog->set_text(TTR("Can not add split meshes as a sibling for the scene root.\nMove the MeshInstance3D below a parent node."));
		error_dialog->popup_centered();
		return;
	}
	Ref<MeshSplitter> splitter;
	splitter.instantiate();
	TypedArray<ArrayMesh> meshes = splitter->split_mesh(node->get_mesh(), settings);
	if (meshes.is_empty()) {
		error_dialog->set_text(TTR("Mesh splitting returned no pieces."));
		error_dialog->popup_centered();
		return;
	}

	Node3D *container = memnew(Node3D);
	container->set_name(String(node->get_name()) + "_Splits");
	container->set_transform(node->get_transform());
	for (int piece_i = 0; piece_i < meshes.size(); piece_i++) {
		Ref<ArrayMesh> mesh = meshes[piece_i];
		if (mesh.is_null()) {
			continue;
		}
		const Vector3 center = mesh->get_meta(SNAME("split_center"), Vector3());
		if (settings->get_output() == MeshSplitSettings::OUTPUT_RIGID_BODIES) {
			RigidBody3D *body = memnew(RigidBody3D);
			body->set_name(vformat("Piece_%04d", piece_i));
			body->set_position(center);
			container->add_child(body);
			MeshInstance3D *mesh_instance = memnew(MeshInstance3D);
			mesh_instance->set_name("Mesh");
			mesh_instance->set_mesh(mesh);
			_mesh_split_copy_material_overrides(node, mesh_instance);
			body->add_child(mesh_instance);
			Ref<ConvexPolygonShape3D> shape = mesh->create_convex_shape(true, false);
			if (shape.is_valid()) {
				CollisionShape3D *collision = memnew(CollisionShape3D);
				collision->set_name("Collision");
				collision->set_shape(shape);
				body->add_child(collision);
			}
		} else {
			MeshInstance3D *mesh_instance = memnew(MeshInstance3D);
			mesh_instance->set_name(vformat("Piece_%04d", piece_i));
			mesh_instance->set_mesh(mesh);
			_mesh_split_copy_material_overrides(node, mesh_instance);
			mesh_instance->set_position(center);
			container->add_child(mesh_instance);
		}
	}

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Create Split MeshInstance3D Parts"));
	Node *owner = get_tree()->get_edited_scene_root();
	undo_redo->add_do_method(node, "add_sibling", container, true);
	undo_redo->add_do_method(container, "set_owner", owner);
	for (int child_i = 0; child_i < container->get_child_count(); child_i++) {
		_mesh_split_add_owner_undo(undo_redo, container->get_child(child_i), owner);
	}
	undo_redo->add_do_reference(container);
	undo_redo->add_undo_method(node->get_parent(), "remove_child", container);
	undo_redo->commit_action();
	dialog->hide();
	inspector->edit(nullptr);
}

MeshSplitterEditor::MeshSplitterEditor() {
	MeshInstance3DEditor *mesh_editor = MeshInstance3DEditor::get_singleton();
	ERR_FAIL_NULL(mesh_editor);
	PopupMenu *mesh_menu = mesh_editor->get_options_menu();
	mesh_menu->add_separator();
	mesh_menu->add_item(TTR("Split Mesh..."), MENU_OPTION_SPLIT_MESH);
	mesh_menu->connect(SceneStringName(id_pressed), callable_mp(this, &MeshSplitterEditor::_menu_option));

	error_dialog = memnew(AcceptDialog);
	add_child(error_dialog);

	dialog = memnew(ConfirmationDialog);
	dialog->set_title(TTR("Split Mesh"));
	dialog->set_ok_button_text(TTR("Create"));
	dialog->set_hide_on_ok(false);
	dialog->set_wrap_controls(false);
	add_child(dialog);
	VBoxContainer *content = memnew(VBoxContainer);
	content->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	content->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	dialog->add_child(content);
	HBoxContainer *preset_controls = memnew(HBoxContainer);
	content->add_child(preset_controls);
	Button *load_button = memnew(Button);
	load_button->set_text(TTR("Load Settings..."));
	load_button->connect(SceneStringName(pressed), callable_mp(this, &MeshSplitterEditor::_popup_load_settings));
	preset_controls->add_child(load_button);
	Button *save_button = memnew(Button);
	save_button->set_text(TTR("Save Settings..."));
	save_button->connect(SceneStringName(pressed), callable_mp(this, &MeshSplitterEditor::_popup_save_settings));
	preset_controls->add_child(save_button);
	Label *hint = memnew(Label);
	hint->set_text(TTR("Triangle-soup splitting supports open meshes. Closed and repaired contours receive cap surfaces."));
	hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	content->add_child(hint);
	inspector = EditorInspector::create_default_inspector();
	inspector->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	inspector->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	content->add_child(inspector);
	dialog->connect(SceneStringName(confirmed), callable_mp(this, &MeshSplitterEditor::_create_parts));

	load_settings_dialog = memnew(EditorFileDialog);
	load_settings_dialog->set_title(TTR("Load Mesh Split Settings"));
	load_settings_dialog->set_access(EditorFileDialog::ACCESS_RESOURCES);
	load_settings_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
	load_settings_dialog->add_filter("*.tres", TTR("Text Resource"));
	load_settings_dialog->add_filter("*.res", TTR("Binary Resource"));
	load_settings_dialog->connect(SNAME("file_selected"), callable_mp(this, &MeshSplitterEditor::_load_settings));
	add_child(load_settings_dialog);

	save_settings_dialog = memnew(EditorFileDialog);
	save_settings_dialog->set_title(TTR("Save Mesh Split Settings"));
	save_settings_dialog->set_access(EditorFileDialog::ACCESS_RESOURCES);
	save_settings_dialog->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
	save_settings_dialog->add_filter("*.tres", TTR("Text Resource"));
	save_settings_dialog->add_filter("*.res", TTR("Binary Resource"));
	save_settings_dialog->connect(SNAME("file_selected"), callable_mp(this, &MeshSplitterEditor::_save_settings));
	add_child(save_settings_dialog);
}

void EditorPluginMeshSplitter::edit(Object *p_object) {
	splitter_editor->edit(Object::cast_to<MeshInstance3D>(p_object));
}

bool EditorPluginMeshSplitter::handles(Object *p_object) const {
	return Object::cast_to<MeshInstance3D>(p_object) != nullptr;
}

void EditorPluginMeshSplitter::make_visible(bool p_visible) {
	splitter_editor->set_visible_for_editor(p_visible);
	if (!p_visible) {
		splitter_editor->edit(nullptr);
	}
}

EditorPluginMeshSplitter::EditorPluginMeshSplitter() {
	splitter_editor = memnew(MeshSplitterEditor);
	EditorNode::get_singleton()->get_gui_base()->add_child(splitter_editor);
}
