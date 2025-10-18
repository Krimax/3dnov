//gcc 3d.c math3d.c -o editor.exe -lgdi32 -luser32 -lcomdlg32 -lmsimg32
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <float.h>
#include <stdio.h>
#include "math3d.h"
#include <math.h>

typedef struct {
    int* items;
    int count;
    int capacity;
} selection_t;
typedef struct {
    int v1;
    int v2;
    int count;
} edge_record_t;
typedef struct {
    vec4_t vertices[3];
    vec3_t colors[3];
} triangle_t;
#define MAX_LIGHTS 32 // Support up to 32 lights in a scene

// A temporary structure to hold pre-calculated light data for one frame
typedef struct {
    vec3_t position;
    vec3_t direction; // For spotlights
    light_t* properties;
} active_light_t;

// --- Global Variables ---
static HWND g_window_handle;
static BITMAPINFO g_framebuffer_info;
static void* g_framebuffer_memory;
static float* g_depth_buffer;
static int g_window_width; 
static int g_window_height;
static HBITMAP g_framebuffer_bitmap = NULL;
static int g_render_width;
static int g_render_height;
static vec4_t* g_clip_coords_buffer = NULL;
static vec3_t* g_colors_buffer = NULL;
static int g_vertex_buffer_capacity = 0;

// --- Specular Lookup Table for powf() optimization ---
#define SPECULAR_TABLE_SIZE 1024
static float g_specular_lookup_table[SPECULAR_TABLE_SIZE];
static float g_current_shininess_in_table = -1.0f; // Track what shininess the table was built for

// Camera and Mouse Input Variables
static float g_camera_distance = 6.0f;
static float g_camera_yaw = 0.0f;
static float g_camera_pitch = 0.3f;
static vec3_t g_camera_target = {0.0f, 0.0f, 0.0f}; // Camera's look-at point
static int g_mouse_down = 0;
static int g_middle_mouse_down = 0; // To track the middle mouse button state
static int g_last_mouse_x = 0;
static int g_last_mouse_y = 0;
static int g_mouse_dragged = 0;
static POINT g_last_right_click_pos;
static int g_is_orthographic = 0; // 0 = Perspective, 1 = Orthographic

// Scene Management Variables
static scene_t g_scene;
static selection_t g_selected_objects;
static selection_t g_selected_components;
static mesh_t* g_cube_mesh_data = NULL;
static mesh_t* g_pyramid_mesh_data = NULL;
static mesh_t* g_sphere_mesh_data = NULL;
static mesh_t* g_vertex_mesh_data = NULL;
static mesh_t* g_edge_mesh_data = NULL;
static mesh_t* g_face_mesh_data = NULL;
static mesh_t* g_player_spawn_mesh_data = NULL;
// --- Edit Mode Variables ---
typedef enum { MODE_OBJECT, MODE_EDIT } operating_mode_t;
typedef enum { EDIT_FACES, EDIT_VERTICES, EDIT_EDGES } edit_component_mode_t;
typedef enum { SHADING_SOLID, SHADING_SMOOTH, SHADING_WIREFRAME } shading_mode_t;

// --- Tool Mode ---
typedef enum {
    TOOL_SELECT,      // Default object selection and transform tool
    TOOL_DRAW_FACE    // New tool for drawing faces
} tool_mode_t;
static tool_mode_t g_current_tool = TOOL_SELECT;
static RECT g_tool_rects[2]; // For the UI buttons
static vec3_t g_last_tile_pos = {FLT_MAX, FLT_MAX, FLT_MAX}; // Tracks the last grid cell a face was drawn in
static float g_draw_plane_height = 0.0f; // NEW: The current Z-height for the drawing plane

// --- Editor Mode ---
typedef enum { EDITOR_MODEL, EDITOR_SCENE } editor_mode_t;
static editor_mode_t g_current_editor_mode = EDITOR_SCENE; // Default to Scene mode
static RECT g_mode_rects[2];

// --- Transform Mode State Variables ---
typedef enum {
    TRANSFORM_NONE,
    TRANSFORM_GRAB,
    TRANSFORM_ROTATE,
    TRANSFORM_SCALE,
    TRANSFORM_LIGHT_INTENSITY
} transform_mode_t;

static transform_mode_t g_current_transform_mode = TRANSFORM_NONE;
static vec3_t g_transform_axis = {0, 0, 0};      // The axis of transformation
static int g_transform_axis_is_locked = 0;     // Has the user locked the axis (X,Y,Z)?
static POINT g_transform_start_mouse_pos;      // Mouse position when transform began

// To store initial state for cancellation
static vec3_t g_transform_initial_position;
static vec3_t g_transform_initial_rotation;
static vec3_t g_transform_initial_scale;
static float g_transform_initial_intensity;
static vec3_t* g_transform_initial_vertices = NULL;

static operating_mode_t g_current_mode = MODE_OBJECT;
static edit_component_mode_t g_edit_mode_component = EDIT_FACES;
static int g_selected_component_index = -1; // Index of selected face/vertex/edge

// --- Function Declarations ---
LRESULT CALLBACK window_callback(HWND, UINT, WPARAM, LPARAM);
void render_frame();
void draw_pixel(int, int, float, uint32_t);
void draw_line(int x0, int y0, float z0, int x1, int y1, float z1, uint32_t color);
void draw_filled_triangle(vec4_t, vec4_t, vec4_t, uint32_t);
void draw_gouraud_triangle(vec4_t p0, vec4_t p1, vec4_t p2, vec3_t c0, vec3_t c1, vec3_t c2);
void draw_vertex_marker(int x, int y, float z, uint32_t c);
void render_object(scene_object_t* object, int object_index, mat4_t view_matrix, mat4_t projection_matrix, vec3_t camera_pos, const active_light_t* lights, int light_count);
void render_grid(mat4_t view_matrix, mat4_t projection_matrix);
int find_clicked_object(int mouse_x, int mouse_y);
int find_clicked_face(scene_object_t* object, int object_index, int mouse_x, int mouse_y);
int find_clicked_vertex(scene_object_t* object, int object_index, int mouse_x, int mouse_y);
int find_clicked_edge(scene_object_t* object, int object_index, int mouse_x, int mouse_y);
vec3_t get_world_pos_on_plane(int mouse_x, int mouse_y);
vec3_t get_selection_world_center(void);
void draw_coordinate_ui(HDC hdc);
void draw_color_ui(HDC hdc);
void destroy_and_apply_coord_edit();
vec3_t get_selection_center(scene_object_t* object);
void draw_pixel_thick(int x, int y, float z, uint32_t color);
void draw_thick_line(int x0, int y0, float z0, int x1, int y1, float z1, uint32_t color);
void normalize_rect(RECT* r);
mesh_t* mesh_copy(const mesh_t* src);
void mesh_calculate_normals(mesh_t* mesh);
void destroy_mesh_data(mesh_t* mesh);
void draw_scene_outliner(HDC hdc);
void draw_mode_ui(HDC hdc);
void draw_outliner_object_recursive(HDC hdc, int object_index, int depth, int* y_offset);
void mesh_add_face(mesh_t* mesh, int v1, int v2, int v3);
void mesh_delete_face(mesh_t* mesh, int face_index_to_delete);
int mesh_add_vertex(mesh_t* mesh, vec3_t vertex);
void selection_init(selection_t* s);
void selection_remove(selection_t* s, int item_to_remove);
void selection_add(selection_t* s, int item);
void selection_clear(selection_t* s);
int selection_contains(const selection_t* s, int item);
void selection_destroy(selection_t* s);
void trigger_save_scene_dialog(void);
void trigger_load_scene_dialog(void);
void scene_set_parent(scene_t* scene, int child_index, int parent_index);
void scene_add_object(scene_t* scene, mesh_t* mesh_data_source, vec3_t pos); 
void model_save_to_file(scene_object_t* object, const char* filename);
void model_load_from_file(scene_t* scene, const char* filename, vec3_t position);
void trigger_save_model_dialog(void);
void trigger_export_selection_dialog(void);
void trigger_load_model_dialog(void);
void scene_destroy(scene_t* scene);
void scene_clear(scene_t* scene);
void create_grid_face_at(vec3_t pos);
// --- UI and Coordinate Editing Variables ---
static HWND g_hEdit = NULL; // Handle to the temporary edit box
static RECT g_coord_rects[3]; // Clickable rectangles for X, Y, Z coordinates
static RECT g_material_rects[2]; // Clickable rectangles for Specular, Shininess
static RECT g_color_swatch_rect;
static int g_editing_coord_axis = -1; // 0=X, 1=Y, 2=Z, 3=Spec, 4=Shine, -1=Not editing
static int g_is_box_selecting = 0;
static RECT g_selection_box_rect;
static shading_mode_t g_shading_mode = SHADING_SOLID;
static RECT g_shading_rects[3];
static vec3_t g_sky_color = {0.1875f, 0.1875f, 0.1875f}; // Default dark grey (0x303030)
static RECT g_sky_color_swatch_rect;
// Menu Command ID
#define ID_DELETE_OBJECT 1001
#define ID_ADD_CUBE 1002
#define ID_ADD_PYRAMID 1003
#define ID_EDIT_COORD 1004
#define ID_ADD_VERTEX 1005
#define ID_ADD_EDGE 1006 
#define ID_ADD_FACE 1007
#define ID_SAVE_SCENE 1008
#define ID_LOAD_SCENE 1009
#define ID_PARENT_OBJECT 1010
#define ID_UNPARENT_OBJECT 1011
#define ID_TOGGLE_DOUBLE_SIDED 1012
#define ID_SAVE_MODEL 1013
#define ID_LOAD_MODEL 1014
#define ID_CLEAR_SCENE 1015
#define ID_TOGGLE_STATIC 1016
#define ID_ADD_POINT_LIGHT 1017
#define ID_ADD_SPOT_LIGHT 1018
#define ID_IMPORT_MODEL 1019
#define ID_EXPORT_MODEL 1020
#define ID_ADD_SPHERE 1021
#define ID_ADD_PLAYER_SPAWN 1022
#define ID_TOGGLE_COLLISION 1023
#define ID_SET_PLAYER_MODEL 1024
#define ID_SET_CAMERA_TARGET 1025

// --- Scene Management ---
void scene_init(scene_t* scene) {
    scene->capacity = 10;
    scene->object_count = 0;
    scene->objects = (scene_object_t**)malloc(scene->capacity * sizeof(scene_object_t*));
}
void scene_clear(scene_t* scene) {
    scene_destroy(scene);
    scene_init(scene);
}
int scene_duplicate_object(scene_t* scene, int source_index) {
    if (!scene || source_index < 0 || source_index >= scene->object_count) {
        return -1;
    }

    scene_object_t* source_obj = scene->objects[source_index];

    if (scene->object_count >= scene->capacity) {
        scene->capacity *= 2;
        scene->objects = (scene_object_t**)realloc(scene->objects, scene->capacity * sizeof(scene_object_t*));
    }

    scene_object_t* new_object = (scene_object_t*)malloc(sizeof(scene_object_t));
    if (!new_object) return -1;

    // --- Perform a deep copy of all relevant properties ---
    
    // Copy mesh data if it exists
    if (source_obj->mesh) {
        new_object->mesh = mesh_copy(source_obj->mesh);
    } else {
        new_object->mesh = NULL;
    }

    // Deep copy light properties if they exist
    if (source_obj->light_properties) {
        new_object->light_properties = (light_t*)malloc(sizeof(light_t));
        memcpy(new_object->light_properties, source_obj->light_properties, sizeof(light_t));
    } else {
        new_object->light_properties = NULL;
    }
    
    new_object->position = source_obj->position;
    new_object->rotation = source_obj->rotation;
    new_object->scale = source_obj->scale;
    // --- MODIFIED: Copy the entire material struct ---
    new_object->material = source_obj->material; 
    new_object->is_double_sided = source_obj->is_double_sided;
    new_object->is_static = source_obj->is_static;
    
    new_object->parent_index = -1;
    new_object->child_count = 0;
    new_object->child_capacity = 4; 
    new_object->children = (int*)malloc(new_object->child_capacity * sizeof(int));

    char base_name[64];
    strcpy_s(base_name, sizeof(base_name), source_obj->name);
    char* dot = strrchr(base_name, '.');
    if (dot && strspn(dot + 1, "0123456789") == strlen(dot + 1)) {
        *dot = '\0';
    }
    
    int max_num = 0;
    for (int i = 0; i < scene->object_count; i++) {
        if (strncmp(scene->objects[i]->name, base_name, strlen(base_name)) == 0) {
            const char* num_part = strrchr(scene->objects[i]->name, '.');
            if (num_part) {
                int num = atoi(num_part + 1);
                if (num > max_num) {
                    max_num = num;
                }
            }
        }
    }
    sprintf_s(new_object->name, sizeof(new_object->name), "%s.%03d", base_name, max_num + 1);

    int new_index = scene->object_count;
    scene->objects[new_index] = new_object;
    scene->object_count++;

    return new_index;
}
void scene_add_light(scene_t* scene, light_type_t type, vec3_t pos) {
    static int point_light_count = 1;
    static int spot_light_count = 1;

    if (scene->object_count >= scene->capacity) {
        scene->capacity *= 2;
        scene->objects = (scene_object_t**)realloc(scene->objects, scene->capacity * sizeof(scene_object_t*));
    }
    scene_object_t* new_light_object = (scene_object_t*)malloc(sizeof(scene_object_t));
    if (!new_light_object) return;

    // --- Core Light Object Setup ---
    new_light_object->mesh = NULL; // Lights don't have a mesh
    new_light_object->light_properties = (light_t*)malloc(sizeof(light_t));
    if (!new_light_object->light_properties) {
        free(new_light_object);
        return;
    }
    new_light_object->light_properties->type = type;
    new_light_object->light_properties->color = (vec3_t){ 1.0f, 1.0f, 1.0f }; // Default white light
    new_light_object->light_properties->intensity = 10.0f;

    // --- Standard Object Properties ---
    new_light_object->position = pos;
    new_light_object->rotation = (vec3_t){ 0, 0, 0 };
    new_light_object->scale = (vec3_t){ 1, 1, 1 };
    
    // --- MODIFIED: Initialize material for gizmo/outliner color ---
    new_light_object->material.diffuse_color = (vec3_t){ 1.0f, 1.0f, 0.0f }; // Gizmo/Outliner color (Yellow)
    new_light_object->material.specular_intensity = 0.0f; // Lights shouldn't be shiny
    new_light_object->material.shininess = 0.0f;

    new_light_object->parent_index = -1;
    new_light_object->child_count = 0;
    new_light_object->child_capacity = 4;
    new_light_object->children = (int*)malloc(new_light_object->child_capacity * sizeof(int));
    new_light_object->is_double_sided = 0;
    new_light_object->is_static = 0;

    if (type == LIGHT_TYPE_POINT) {
        sprintf_s(new_light_object->name, sizeof(new_light_object->name), "PointLight.%03d", point_light_count++);
    } else if (type == LIGHT_TYPE_SPOT) {
        sprintf_s(new_light_object->name, sizeof(new_light_object->name), "SpotLight.%03d", spot_light_count++);
        // Set default spot light properties
        new_light_object->light_properties->spot_angle = 3.14159f / 4.0f; // 45 degrees
        new_light_object->light_properties->spot_blend = 0.15f;
    }

    scene->objects[scene->object_count++] = new_light_object;
}
void create_grid_face_at(vec3_t pos) {
    // 1. Create a new, temporary mesh for a 1x1 quad
    mesh_t* quad_mesh = (mesh_t*)malloc(sizeof(mesh_t));
    if (!quad_mesh) return;

    quad_mesh->vertex_count = 4;
    quad_mesh->vertices = (vec3_t*)malloc(quad_mesh->vertex_count * sizeof(vec3_t));
    
    // 2. Define the four corners of the quad in world space around the given position
    quad_mesh->vertices[0] = (vec3_t){ pos.x - 0.5f, pos.y - 0.5f, pos.z };
    quad_mesh->vertices[1] = (vec3_t){ pos.x + 0.5f, pos.y - 0.5f, pos.z };
    quad_mesh->vertices[2] = (vec3_t){ pos.x + 0.5f, pos.y + 0.5f, pos.z };
    quad_mesh->vertices[3] = (vec3_t){ pos.x - 0.5f, pos.y + 0.5f, pos.z };

    // 3. Define the two triangles that make up the quad
    quad_mesh->face_count = 2;
    quad_mesh->faces = (int*)malloc(quad_mesh->face_count * 3 * sizeof(int));
    quad_mesh->faces[0] = 0; quad_mesh->faces[1] = 1; quad_mesh->faces[2] = 2;
    quad_mesh->faces[3] = 0; quad_mesh->faces[4] = 2; quad_mesh->faces[5] = 3;
    
    // 4. Calculate normals for lighting
    quad_mesh->normals = NULL;
    mesh_calculate_normals(quad_mesh);

    // 5. Add this new mesh as an object to the scene.
    // NOTE: The object's position is (0,0,0) because the vertices are already in world space.
    scene_add_object(&g_scene, quad_mesh, (vec3_t){0,0,0});

    // 6. Clean up the temporary mesh, since scene_add_object made a copy of it.
    destroy_mesh_data(quad_mesh);
}
void scene_add_object(scene_t* scene, mesh_t* mesh_data_source, vec3_t pos) {
    static int cube_count = 1;
    static int pyramid_count = 1;
    static int sphere_count = 1;
    static int vertex_count = 1;
    static int edge_count = 1;
    static int face_count = 1;
    static int generic_count = 1;

    if (scene->object_count >= scene->capacity) {
        scene->capacity *= 2;
        scene->objects = (scene_object_t**)realloc(scene->objects, scene->capacity * sizeof(scene_object_t*));
    }
    scene_object_t* new_object = (scene_object_t*)malloc(sizeof(scene_object_t));
    if (!new_object) return;

    new_object->mesh = mesh_copy(mesh_data_source);
    if (!new_object->mesh) {
        free(new_object);
        return;
    }
    
    new_object->light_properties = NULL; 
    new_object->is_player_spawn = 0;
    new_object->is_player_model = 0;
    new_object->camera_offset = (vec3_t){0, 0, 0};

    if (g_current_editor_mode == EDITOR_MODEL) {
        new_object->position = (vec3_t){ 0, 0, 0 };
    } else {
        new_object->position = pos;
    }
    new_object->rotation = (vec3_t){ 0, 0, 0 };
    new_object->scale = (vec3_t){ 1, 1, 1 };
    
    new_object->material.diffuse_color = (vec3_t){ 0.8f, 0.8f, 0.8f }; 
    new_object->material.specular_intensity = 0.5f;
    new_object->material.shininess = 32.0f;       

    new_object->parent_index = -1;
    new_object->child_count = 0;
    new_object->child_capacity = 4;
    new_object->children = (int*)malloc(new_object->child_capacity * sizeof(int));
    new_object->is_double_sided = 1;
    new_object->is_static = 0;
    new_object->has_collision = 1; // Default to having collision

    if (mesh_data_source == g_player_spawn_mesh_data) {
        sprintf_s(new_object->name, sizeof(new_object->name), "PlayerSpawn");
        new_object->material.diffuse_color = (vec3_t){1.0f, 0.0f, 0.0f};
        new_object->is_player_spawn = 1;
        new_object->has_collision = 0; // The spawn point itself shouldn't have collision
    } else if (mesh_data_source == g_cube_mesh_data) {
        sprintf_s(new_object->name, sizeof(new_object->name), "Cube.%03d", cube_count++);
    } else if (mesh_data_source == g_pyramid_mesh_data) {
        sprintf_s(new_object->name, sizeof(new_object->name), "Pyramid.%03d", pyramid_count++);
    } else if (mesh_data_source == g_sphere_mesh_data) {
        sprintf_s(new_object->name, sizeof(new_object->name), "Sphere.%03d", sphere_count++);
    } else if (mesh_data_source == g_vertex_mesh_data) {
        sprintf_s(new_object->name, sizeof(new_object->name), "Vertex.%03d", vertex_count++);
    } else if (mesh_data_source == g_edge_mesh_data) {
        sprintf_s(new_object->name, sizeof(new_object->name), "Edge.%03d", edge_count++);
    } else if (mesh_data_source == g_face_mesh_data) {
        sprintf_s(new_object->name, sizeof(new_object->name), "Face.%03d", face_count++);
    } else {
        const char *base_name = "Object";
        int max_num = 0;
        for (int i = 0; i < scene->object_count; i++) {
            if (strncmp(scene->objects[i]->name, base_name, strlen(base_name)) == 0) {
                 const char* num_part = strrchr(scene->objects[i]->name, '.');
                 if(num_part) {
                     int num = atoi(num_part + 1);
                     if (num > max_num) max_num = num;
                 }
            }
        }
        sprintf_s(new_object->name, sizeof(new_object->name), "%s.%03d", base_name, max_num + 1);
    }

    scene->objects[scene->object_count] = new_object;
    scene->object_count++;
}
void scene_remove_object(scene_t* scene, int index_to_remove) {
    if (!scene || index_to_remove < 0 || index_to_remove >= scene->object_count) {
        return;
    }
    scene_object_t* obj_to_remove = scene->objects[index_to_remove];

    // --- HIERARCHY MANAGEMENT ---
    // 1. Unparent all children of the object being deleted so they become root objects.
    // We must iterate backwards as scene_set_parent might modify the children array.
    while (obj_to_remove->child_count > 0) {
        // The child's index is the last element in the children array.
        int child_index = obj_to_remove->children[obj_to_remove->child_count - 1];
        // Set the child's parent to -1 (no parent). This also removes it from this object's children list.
        scene_set_parent(scene, child_index, -1);
    }

    // 2. Remove this object from its own parent's list (if it has one).
    if (obj_to_remove->parent_index != -1) {
        scene_set_parent(scene, index_to_remove, -1);
    }
    // --- END HIERARCHY MANAGEMENT ---

    // Free the object's own memory
    destroy_mesh_data(obj_to_remove->mesh);
    if (obj_to_remove->light_properties) {
        free(obj_to_remove->light_properties); // <-- NEW: Free light properties
    }
    free(obj_to_remove->children);
    free(obj_to_remove);

    // Shift the main object array to close the gap
    for (int i = index_to_remove; i < scene->object_count - 1; i++) {
        scene->objects[i] = scene->objects[i + 1];
    }
    scene->object_count--;

    // --- CRITICAL: Remap all indices in the scene ---
    // Since we shifted the array, any index greater than the one we removed is now off by one.
    for (int i = 0; i < scene->object_count; i++) {
        scene_object_t* current_obj = scene->objects[i];
        
        // Update the object's own parent index if it was affected by the shift
        if (current_obj->parent_index > index_to_remove) {
            current_obj->parent_index--;
        }
        
        // Update the indices of all children in its list
        for (int j = 0; j < current_obj->child_count; j++) {
            if (current_obj->children[j] > index_to_remove) {
                current_obj->children[j]--;
            }
        }
    }
}
void scene_destroy(scene_t* scene) {
    if (!scene || !scene->objects) return;
    for (int i = 0; i < scene->object_count; i++) {
        if (scene->objects[i]) {
            destroy_mesh_data(scene->objects[i]->mesh);
            if (scene->objects[i]->light_properties) { // NEW
                free(scene->objects[i]->light_properties);
            }
            free(scene->objects[i]->children);
            free(scene->objects[i]);
        }
    }
    free(scene->objects);
    scene->objects = NULL;
    scene->object_count = 0;
    scene->capacity = 0;
}
int scene_load_from_file(scene_t* scene, const char* filename) {
    if (!scene || !filename) return 0;

    FILE* file = fopen(filename, "rb");
    if (!file) {
        MessageBox(NULL, "Failed to open file for reading.", "Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    char header[4];
    fread(header, sizeof(char), 4, file);

    int is_scn3_format = (strncmp(header, "SCN3", 4) == 0);
    int is_scn2_format = (strncmp(header, "SCN2", 4) == 0);
    int is_scn1_format = (strncmp(header, "SCN1", 4) == 0);

    int object_count = 0;
    if (is_scn3_format || is_scn2_format) {
        fread(&g_sky_color, sizeof(vec3_t), 1, file);
        fread(&object_count, sizeof(int), 1, file);
    } else if (is_scn1_format) {
        g_sky_color = (vec3_t){0.1875f, 0.1875f, 0.1875f}; 
        fread(&object_count, sizeof(int), 1, file);
    } else {
        g_sky_color = (vec3_t){0.1875f, 0.1875f, 0.1875f};
        fseek(file, 0, SEEK_SET);
        fread(&object_count, sizeof(int), 1, file);
    }

    scene_destroy(scene);
    scene_init(scene);

    for (int i = 0; i < object_count; i++) {
        if (scene->object_count >= scene->capacity) {
            scene->capacity *= 2;
            scene->objects = (scene_object_t**)realloc(scene->objects, scene->capacity * sizeof(scene_object_t*));
        }
        
        scene_object_t* new_obj = (scene_object_t*)malloc(sizeof(scene_object_t));
        if (!new_obj) continue;
        
        new_obj->child_count = 0;
        new_obj->child_capacity = 4;
        new_obj->children = (int*)malloc(new_obj->child_capacity * sizeof(int));
        
        fread(&new_obj->position, sizeof(vec3_t), 1, file);
        fread(&new_obj->rotation, sizeof(vec3_t), 1, file);
        fread(&new_obj->scale, sizeof(vec3_t), 1, file);
        
        if (is_scn3_format || is_scn2_format) {
            fread(&new_obj->material, sizeof(material_t), 1, file);
        } else if (is_scn1_format) {
            vec3_t old_color;
            fread(&old_color, sizeof(vec3_t), 1, file);
            new_obj->material.diffuse_color = old_color;
            new_obj->material.specular_intensity = 0.5f;
            new_obj->material.shininess = 32.0f;
        } else {
            new_obj->material.diffuse_color = (vec3_t){0.8f, 0.8f, 0.8f};
            new_obj->material.specular_intensity = 0.5f;
            new_obj->material.shininess = 32.0f;
        }
        
        if (is_scn3_format) {
             fread(&new_obj->parent_index, sizeof(int), 1, file);
             fread(&new_obj->is_double_sided, sizeof(int), 1, file);
             fread(&new_obj->is_static, sizeof(int), 1, file);
             fread(&new_obj->is_player_spawn, sizeof(int), 1, file);
             fread(&new_obj->has_collision, sizeof(int), 1, file);
             fread(&new_obj->is_player_model, sizeof(int), 1, file);
             fread(&new_obj->camera_offset, sizeof(vec3_t), 1, file);
        } else if (is_scn2_format) {
             fread(&new_obj->parent_index, sizeof(int), 1, file);
             fread(&new_obj->is_double_sided, sizeof(int), 1, file);
             fread(&new_obj->is_static, sizeof(int), 1, file);
             fread(&new_obj->is_player_spawn, sizeof(int), 1, file);
             fread(&new_obj->has_collision, sizeof(int), 1, file);
             new_obj->is_player_model = 0;
             new_obj->camera_offset = (vec3_t){0,0,0};
        } else if (is_scn1_format) {
             fread(&new_obj->parent_index, sizeof(int), 1, file);
             fread(&new_obj->is_double_sided, sizeof(int), 1, file);
             fread(&new_obj->is_static, sizeof(int), 1, file);
             new_obj->is_player_spawn = 0;
             new_obj->has_collision = 1; // <-- SET DEFAULT
             new_obj->is_player_model = 0;
             new_obj->camera_offset = (vec3_t){0,0,0};
        } else {
             new_obj->parent_index = -1;
             new_obj->is_double_sided = 0;
             new_obj->is_static = 0;
             new_obj->is_player_spawn = 0;
             new_obj->has_collision = 1; // <-- SET DEFAULT
             new_obj->is_player_model = 0;
             new_obj->camera_offset = (vec3_t){0,0,0};
        }


        int is_light = 0;
        if (is_scn3_format || is_scn2_format || is_scn1_format) {
            fread(&is_light, sizeof(int), 1, file);
        }

        if (is_light) {
            new_obj->mesh = NULL;
            new_obj->light_properties = (light_t*)malloc(sizeof(light_t));
            fread(new_obj->light_properties, sizeof(light_t), 1, file);
        } else {
            new_obj->light_properties = NULL;
            new_obj->mesh = (mesh_t*)malloc(sizeof(mesh_t));
            if (!new_obj->mesh) { free(new_obj->children); free(new_obj); continue; }
            new_obj->mesh->normals = NULL;

            fread(&new_obj->mesh->vertex_count, sizeof(int), 1, file);
            new_obj->mesh->vertices = (new_obj->mesh->vertex_count > 0) ? (vec3_t*)malloc(new_obj->mesh->vertex_count * sizeof(vec3_t)) : NULL;
            if (new_obj->mesh->vertices) fread(new_obj->mesh->vertices, sizeof(vec3_t), new_obj->mesh->vertex_count, file);

            fread(&new_obj->mesh->face_count, sizeof(int), 1, file);
            new_obj->mesh->faces = (new_obj->mesh->face_count > 0) ? (int*)malloc(new_obj->mesh->face_count * 3 * sizeof(int)) : NULL;
            if (new_obj->mesh->faces) fread(new_obj->mesh->faces, sizeof(int), new_obj->mesh->face_count * 3, file);

            mesh_calculate_normals(new_obj->mesh);
        }

        scene->objects[scene->object_count++] = new_obj;
    }

    // --- PASS 2: Link children to their parents ---
    for (int i = 0; i < scene->object_count; i++) {
        int parent_idx = scene->objects[i]->parent_index;
        if (parent_idx != -1 && parent_idx < scene->object_count) {
            scene_object_t* parent_obj = scene->objects[parent_idx];
            if (parent_obj->child_count >= parent_obj->child_capacity) {
                parent_obj->child_capacity *= 2;
                parent_obj->children = (int*)realloc(parent_obj->children, parent_obj->child_capacity * sizeof(int));
            }
            parent_obj->children[parent_obj->child_count++] = i;
        }
    }

    fclose(file);
    return 1;
}
void scene_set_parent(scene_t* scene, int child_index, int parent_index) {
    // 1. --- VALIDATION ---
    if (!scene || child_index < 0 || child_index >= scene->object_count) return;
    if (parent_index >= scene->object_count) return; // parent_index can be -1 to unparent
    if (child_index == parent_index) return; // Can't parent to self

    scene_object_t* child_obj = scene->objects[child_index];

    // Prevent circular dependencies (parenting an object to its own descendant)
    int current_ancestor = parent_index;
    while (current_ancestor != -1) {
        if (current_ancestor == child_index) return; // Abort if a circle is detected
        current_ancestor = scene->objects[current_ancestor]->parent_index;
    }

    // --- TRANSFORM PRESERVATION ---
    // Get the child's current world transform *before* we change its parent.
    mat4_t child_world_transform = mat4_get_world_transform(scene, child_index);

    // 2. --- UNLINK FROM OLD PARENT ---
    int old_parent_index = child_obj->parent_index;
    if (old_parent_index != -1) {
        scene_object_t* old_parent_obj = scene->objects[old_parent_index];
        int child_found_at = -1;
        // Find the child in the old parent's children list
        for (int i = 0; i < old_parent_obj->child_count; i++) {
            if (old_parent_obj->children[i] == child_index) {
                child_found_at = i;
                break;
            }
        }
        // Remove it by shifting the rest of the list
        if (child_found_at != -1) {
            for (int i = child_found_at; i < old_parent_obj->child_count - 1; i++) {
                old_parent_obj->children[i] = old_parent_obj->children[i + 1];
            }
            old_parent_obj->child_count--;
        }
    }

    // 3. --- LINK TO NEW PARENT ---
    child_obj->parent_index = parent_index;
    if (parent_index != -1) {
        scene_object_t* new_parent_obj = scene->objects[parent_index];
        // Expand the children array if necessary
        if (new_parent_obj->child_count >= new_parent_obj->child_capacity) {
            new_parent_obj->child_capacity *= 2;
            new_parent_obj->children = (int*)realloc(new_parent_obj->children, new_parent_obj->child_capacity * sizeof(int));
        }
        // Add child to the new parent's list
        new_parent_obj->children[new_parent_obj->child_count++] = child_index;
    }

    // 4. --- UPDATE CHILD'S LOCAL TRANSFORM ---
    // Calculate the new local transform that keeps the object in the same world space.
    mat4_t new_parent_world_transform = mat4_get_world_transform(scene, parent_index);
    mat4_t inv_new_parent_transform = mat4_inverse(new_parent_world_transform);

    // New Local = Inverse(Parent World) * Old Child World
    mat4_t new_child_local_transform = mat4_mul_mat4(inv_new_parent_transform, child_world_transform);

    // A full engine would decompose the matrix into position, rotation, and scale.
    // For simplicity, we will only update the position. This works well unless the
    // parent has complex rotation and scale.
    child_obj->position.x = new_child_local_transform.m[0][3];
    child_obj->position.y = new_child_local_transform.m[1][3];
    child_obj->position.z = new_child_local_transform.m[2][3];
}
void model_save_to_file(scene_object_t* object, const char* filename) {
    if (!object || !object->mesh || !filename) return;

    FILE* file = fopen(filename, "wb");
    if (!file) {
        MessageBox(NULL, "Failed to open file for writing.", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    // Write a simple header: magic number "MDL1"
    char header[4] = "MDL1";
    fwrite(header, sizeof(char), 4, file);

    mesh_t* mesh = object->mesh;

    // Write mesh data
    fwrite(&mesh->vertex_count, sizeof(int), 1, file);
    if (mesh->vertex_count > 0) {
        fwrite(mesh->vertices, sizeof(vec3_t), mesh->vertex_count, file);
    }

    fwrite(&mesh->face_count, sizeof(int), 1, file);
    if (mesh->face_count > 0) {
        fwrite(mesh->faces, sizeof(int), mesh->face_count * 3, file);
    }

    fclose(file);
}
void model_load_from_file(scene_t* scene, const char* filename, vec3_t position) {
    if (!scene || !filename) return;

    if (g_current_editor_mode == EDITOR_MODEL && scene->object_count > 0) {
        MessageBox(g_window_handle, "Model Mode only supports one object. Use 'New Model' to start over.", "Action Blocked", MB_OK | MB_ICONINFORMATION);
        return;
    }

    FILE* file = fopen(filename, "rb");
    if (!file) {
        MessageBox(NULL, "Failed to open model file for reading.", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    // Check header
    char header[4];
    fread(header, sizeof(char), 4, file);
    if (strncmp(header, "MDL1", 4) != 0) {
        MessageBox(NULL, "Invalid model file format.", "Error", MB_OK | MB_ICONERROR);
        fclose(file);
        return;
    }

    mesh_t* new_mesh = (mesh_t*)malloc(sizeof(mesh_t));
    if (!new_mesh) {
        fclose(file);
        return;
    }
    new_mesh->normals = NULL; // Initialize normals pointer

    // Read vertex data
    fread(&new_mesh->vertex_count, sizeof(int), 1, file);
    if (new_mesh->vertex_count > 0) {
        new_mesh->vertices = (vec3_t*)malloc(new_mesh->vertex_count * sizeof(vec3_t));
        fread(new_mesh->vertices, sizeof(vec3_t), new_mesh->vertex_count, file);
    } else {
        new_mesh->vertices = NULL;
    }

    // Read face data
    fread(&new_mesh->face_count, sizeof(int), 1, file);
    if (new_mesh->face_count > 0) {
        new_mesh->faces = (int*)malloc(new_mesh->face_count * 3 * sizeof(int));
        fread(new_mesh->faces, sizeof(int), new_mesh->face_count * 3, file);
    } else {
        new_mesh->faces = NULL;
    }

    fclose(file);
    
    // --- Calculate normals after loading ---
    mesh_calculate_normals(new_mesh);

    // Now, add this mesh as a new object to the scene, passing the position
    scene_add_object(scene, new_mesh, position);

    // scene_add_object creates a copy, so we must free the temporary mesh we loaded into
    destroy_mesh_data(new_mesh);
}

void trigger_load_model_dialog(void) {
    OPENFILENAME ofn = {0};
    char szFile[260] = {0};

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_window_handle;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Model Files\0*.model\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileName(&ofn) == TRUE) {
        // We need the world position from where the user right-clicked
        // to place the new object.
        POINT client_pos = g_last_right_click_pos;
        ScreenToClient(g_window_handle, &client_pos);
        vec3_t world_pos = get_world_pos_on_plane(client_pos.x, client_pos.y);

        model_load_from_file(&g_scene, ofn.lpstrFile, world_pos);
    }
}
void scene_save_to_file(scene_t* scene, const char* filename) {
    if (!scene || !filename) return;

    FILE* file = fopen(filename, "wb");
    if (!file) {
        MessageBox(NULL, "Failed to open file for writing.", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    char header[4] = "SCN3"; // NEW FORMAT
    fwrite(header, sizeof(char), 4, file);
    fwrite(&g_sky_color, sizeof(vec3_t), 1, file);
    fwrite(&scene->object_count, sizeof(int), 1, file);

    for (int i = 0; i < scene->object_count; i++) {
        scene_object_t* obj = scene->objects[i];

        fwrite(&obj->position, sizeof(vec3_t), 1, file);
        fwrite(&obj->rotation, sizeof(vec3_t), 1, file);
        fwrite(&obj->scale, sizeof(vec3_t), 1, file);
        
        fwrite(&obj->material, sizeof(material_t), 1, file);

        fwrite(&obj->parent_index, sizeof(int), 1, file);
        fwrite(&obj->is_double_sided, sizeof(int), 1, file);
        fwrite(&obj->is_static, sizeof(int), 1, file);
        fwrite(&obj->is_player_spawn, sizeof(int), 1, file);
        fwrite(&obj->has_collision, sizeof(int), 1, file);
        fwrite(&obj->is_player_model, sizeof(int), 1, file); // <-- NEW
        fwrite(&obj->camera_offset, sizeof(vec3_t), 1, file); // <-- NEW


        int is_light = (obj->light_properties != NULL);
        fwrite(&is_light, sizeof(int), 1, file);

        if (is_light) {
            fwrite(obj->light_properties, sizeof(light_t), 1, file);
        } else {
            fwrite(&obj->mesh->vertex_count, sizeof(int), 1, file);
            if (obj->mesh->vertex_count > 0) {
                fwrite(obj->mesh->vertices, sizeof(vec3_t), obj->mesh->vertex_count, file);
            }

            fwrite(&obj->mesh->face_count, sizeof(int), 1, file);
            if (obj->mesh->face_count > 0) {
                fwrite(obj->mesh->faces, sizeof(int), obj->mesh->face_count * 3, file);
            }
        }
    }

    fclose(file);
}
void selection_init(selection_t* s) {
    s->count = 0;
    s->capacity = 10; // Start with a reasonable capacity
    s->items = (int*)malloc(s->capacity * sizeof(int));
}

void selection_add(selection_t* s, int item) {
    if (!s || !s->items) return;
    // Check for duplicates
    for (int i = 0; i < s->count; i++) {
        if (s->items[i] == item) return;
    }
    // Resize if necessary
    if (s->count >= s->capacity) {
        s->capacity *= 2;
        s->items = (int*)realloc(s->items, s->capacity * sizeof(int));
    }
    s->items[s->count++] = item;
}
void selection_remove(selection_t* s, int item_to_remove) {
    if (!s || s->count == 0) return;

    int found_index = -1;
    // Find the index of the item to remove
    for (int i = 0; i < s->count; i++) {
        if (s->items[i] == item_to_remove) {
            found_index = i;
            break;
        }
    }

    // If the item was found, shift the rest of the array down
    if (found_index != -1) {
        for (int i = found_index; i < s->count - 1; i++) {
            s->items[i] = s->items[i + 1];
        }
        s->count--;
    }
}
void selection_clear(selection_t* s) {
    if (s) s->count = 0;
}

// Checks if a specific item index is in the selection list
int selection_contains(const selection_t* s, int item) {
    if (!s || !s->items) return 0;
    for (int i = 0; i < s->count; i++) {
        if (s->items[i] == item) return 1;
    }
    return 0;
}

void selection_destroy(selection_t* s) {
    if (s && s->items) {
        free(s->items);
        s->items = NULL;
        s->count = 0;
        s->capacity = 0;
    }
}
void normalize_rect(RECT* r) {
    if (r->left > r->right) {
        int temp = r->left;
        r->left = r->right;
        r->right = temp;
    }
    if (r->top > r->bottom) {
        int temp = r->top;
        r->top = r->bottom;
        r->bottom = temp;
    }
}
// --- Mesh Creation ---
int mesh_add_vertex(mesh_t* mesh, vec3_t vertex) {
    if (!mesh) return -1;

    int new_vertex_count = mesh->vertex_count + 1;
    vec3_t* new_vertices = (vec3_t*)realloc(mesh->vertices, new_vertex_count * sizeof(vec3_t));
    if (!new_vertices) {
        return -1; // Allocation failure
    }
    mesh->vertices = new_vertices;
    
    // Add the new vertex at the end
    mesh->vertices[mesh->vertex_count] = vertex;
    
    // Return the index of the newly added vertex
    return mesh->vertex_count++;
}
mesh_t* mesh_copy(const mesh_t* src) {
    if (!src) return NULL;
    
    mesh_t* dst = (mesh_t*)malloc(sizeof(mesh_t));
    if (!dst) return NULL;

    // Copy vertices
    dst->vertex_count = src->vertex_count;
    if (dst->vertex_count > 0) {
        size_t vertices_size = dst->vertex_count * sizeof(vec3_t);
        dst->vertices = (vec3_t*)malloc(vertices_size);
        if (!dst->vertices) { free(dst); return NULL; }
        memcpy(dst->vertices, src->vertices, vertices_size);
    } else {
        dst->vertices = NULL;
    }

    // Copy faces
    dst->face_count = src->face_count;
    if (dst->face_count > 0) {
        size_t faces_size = dst->face_count * 3 * sizeof(int);
        dst->faces = (int*)malloc(faces_size);
        if (!dst->faces) { free(dst->vertices); free(dst); return NULL; }
        memcpy(dst->faces, src->faces, faces_size);
    } else {
        dst->faces = NULL;
    }

    // --- NEW: Copy normals ---
    if (src->normals && src->vertex_count > 0) {
        size_t normals_size = dst->vertex_count * sizeof(vec3_t);
        dst->normals = (vec3_t*)malloc(normals_size);
        if (!dst->normals) {
            if(dst->faces) free(dst->faces);
            if(dst->vertices) free(dst->vertices);
            free(dst);
            return NULL;
        }
        memcpy(dst->normals, src->normals, normals_size);
    } else {
        dst->normals = NULL;
    }

    return dst;
}
void mesh_calculate_normals(mesh_t* mesh) {
    if (!mesh || mesh->vertex_count == 0 || mesh->face_count == 0) {
        if (mesh && mesh->normals) {
            free(mesh->normals);
            mesh->normals = NULL;
        }
        return;
    }

    // Allocate or reallocate memory for normals
    if (mesh->normals) {
        free(mesh->normals);
    }
    mesh->normals = (vec3_t*)calloc(mesh->vertex_count, sizeof(vec3_t));
    if (!mesh->normals) return;

    // Iterate over each face to calculate its normal and add it to the vertex normals
    for (int i = 0; i < mesh->face_count; i++) {
        int v0_idx = mesh->faces[i * 3 + 0];
        int v1_idx = mesh->faces[i * 3 + 1];
        int v2_idx = mesh->faces[i * 3 + 2];

        vec3_t v0 = mesh->vertices[v0_idx];
        vec3_t v1 = mesh->vertices[v1_idx];
        vec3_t v2 = mesh->vertices[v2_idx];

        vec3_t edge1 = vec3_sub(v1, v0);
        vec3_t edge2 = vec3_sub(v2, v0);
        vec3_t face_normal = vec3_cross(edge1, edge2);

        // Add the face normal to each of the face's vertices
        mesh->normals[v0_idx] = vec3_add(mesh->normals[v0_idx], face_normal);
        mesh->normals[v1_idx] = vec3_add(mesh->normals[v1_idx], face_normal);
        mesh->normals[v2_idx] = vec3_add(mesh->normals[v2_idx], face_normal);
    }

    // Normalize all the vertex normals
    for (int i = 0; i < mesh->vertex_count; i++) {
        mesh->normals[i] = vec3_normalize(mesh->normals[i]);
    }
}
void mesh_delete_face(mesh_t* mesh, int face_index_to_delete) {
    if (!mesh || face_index_to_delete < 0 || face_index_to_delete >= mesh->face_count) {
        return;
    }

    // To remove the face, we shift all subsequent faces down by one
    // The face data is 3 integers (v1, v2, v3)
    int start_of_deleted = face_index_to_delete * 3;
    int start_of_next = (face_index_to_delete + 1) * 3;
    int num_ints_to_move = (mesh->face_count - 1 - face_index_to_delete) * 3;

    if (num_ints_to_move > 0) {
        // Use memmove because the memory regions might overlap
        memmove(&mesh->faces[start_of_deleted], &mesh->faces[start_of_next], num_ints_to_move * sizeof(int));
    }

    mesh->face_count--;

    // Optionally, reallocate to a smaller memory block to save space,
    // but for simplicity, we can skip this for now. It's a minor optimization.
    
    mesh_calculate_normals(mesh);
}
void mesh_add_face(mesh_t* mesh, int v1, int v2, int v3) {
    if (!mesh) return;

    // Reallocate the faces array to make space for one more face (3 ints)
    int new_face_count = mesh->face_count + 1;
    int* new_faces = (int*)realloc(mesh->faces, new_face_count * 3 * sizeof(int));
    if (!new_faces) {
        // Handle allocation failure
        return;
    }
    mesh->faces = new_faces;

    // Add the new face indices at the end of the array
    int new_face_start_index = mesh->face_count * 3;
    mesh->faces[new_face_start_index + 0] = v1;
    mesh->faces[new_face_start_index + 1] = v2;
    mesh->faces[new_face_start_index + 2] = v3;

    mesh->face_count = new_face_count;
    
    mesh_calculate_normals(mesh); 
}
mesh_t* create_vertex_mesh(void) {
    mesh_t* mesh = (mesh_t*)malloc(sizeof(mesh_t));
    if (!mesh) return NULL;
    mesh->vertex_count = 1;
    mesh->vertices = (vec3_t*)malloc(sizeof(vec3_t));
    mesh->vertices[0] = (vec3_t){0, 0, 0};
    mesh->face_count = 0;
    mesh->faces = NULL;
    return mesh;
}

mesh_t* create_edge_mesh(void) {
    mesh_t* mesh = (mesh_t*)malloc(sizeof(mesh_t));
    if (!mesh) return NULL;
    mesh->vertex_count = 2;
    mesh->vertices = (vec3_t*)malloc(2 * sizeof(vec3_t));
    mesh->vertices[0] = (vec3_t){-0.5f, 0, 0};
    mesh->vertices[1] = (vec3_t){0.5f, 0, 0};
    mesh->face_count = 0;
    mesh->faces = NULL;
    return mesh;
}

mesh_t* create_face_mesh(void) {
    mesh_t* mesh = (mesh_t*)malloc(sizeof(mesh_t));
    if (!mesh) return NULL;
    mesh->vertex_count = 3;
    mesh->vertices = (vec3_t*)malloc(3 * sizeof(vec3_t));
    mesh->vertices[0] = (vec3_t){-0.5f, -0.5f, 0};
    mesh->vertices[1] = (vec3_t){0.5f, -0.5f, 0};
    mesh->vertices[2] = (vec3_t){0.0f, 0.5f, 0};
    mesh->face_count = 1;
    mesh->faces = (int*)malloc(3 * sizeof(int));
    // Define the single triangle face. Winding order is important for backface culling.
    mesh->faces[0] = 0;
    mesh->faces[1] = 2;
    mesh->faces[2] = 1;
    return mesh;
}
mesh_t* create_cube_mesh(void) {
    mesh_t* mesh = (mesh_t*)malloc(sizeof(mesh_t));
    if (!mesh) return NULL;
    mesh->vertex_count = 8;
    mesh->vertices = (vec3_t*)malloc(mesh->vertex_count * sizeof(vec3_t));
    // Bottom face vertices (Z = -0.5)
    mesh->vertices[0] = (vec3_t){-0.5f, -0.5f, -0.5f}; // 0: left-back-bottom
    mesh->vertices[1] = (vec3_t){ 0.5f, -0.5f, -0.5f}; // 1: right-back-bottom
    mesh->vertices[2] = (vec3_t){ 0.5f,  0.5f, -0.5f}; // 2: right-front-bottom
    mesh->vertices[3] = (vec3_t){-0.5f,  0.5f, -0.5f}; // 3: left-front-bottom
    // Top face vertices (Z = 0.5)
    mesh->vertices[4] = (vec3_t){-0.5f, -0.5f,  0.5f}; // 4: left-back-top
    mesh->vertices[5] = (vec3_t){ 0.5f, -0.5f,  0.5f}; // 5: right-back-top
    mesh->vertices[6] = (vec3_t){ 0.5f,  0.5f,  0.5f}; // 6: right-front-top
    mesh->vertices[7] = (vec3_t){-0.5f,  0.5f,  0.5f}; // 7: left-front-top
    
    mesh->face_count = 12;
    mesh->faces = (int*)malloc(mesh->face_count * 3 * sizeof(int));
    int i=0;
    
    // Correct Counter-Clockwise winding for all faces (viewed from outside)
    // Front face (+Y)
    mesh->faces[i++]=2; mesh->faces[i++]=3; mesh->faces[i++]=7;
    mesh->faces[i++]=2; mesh->faces[i++]=7; mesh->faces[i++]=6;
    // Back face (-Y)
    mesh->faces[i++]=1; mesh->faces[i++]=5; mesh->faces[i++]=4;
    mesh->faces[i++]=1; mesh->faces[i++]=4; mesh->faces[i++]=0;
    // Top face (+Z)
    mesh->faces[i++]=4; mesh->faces[i++]=5; mesh->faces[i++]=6;
    mesh->faces[i++]=4; mesh->faces[i++]=6; mesh->faces[i++]=7;
    // Bottom face (-Z)
    mesh->faces[i++]=0; mesh->faces[i++]=3; mesh->faces[i++]=2;
    mesh->faces[i++]=0; mesh->faces[i++]=2; mesh->faces[i++]=1;
    // Right face (+X)
    mesh->faces[i++]=1; mesh->faces[i++]=2; mesh->faces[i++]=6;
    mesh->faces[i++]=1; mesh->faces[i++]=6; mesh->faces[i++]=5;
    // Left face (-X)
    mesh->faces[i++]=0; mesh->faces[i++]=4; mesh->faces[i++]=7;
    mesh->faces[i++]=0; mesh->faces[i++]=7; mesh->faces[i++]=3;
    
    mesh->normals = NULL;
    mesh_calculate_normals(mesh);
    return mesh;
}
mesh_t* create_sphere_mesh(int segments, int rings) {
    if (segments < 3 || rings < 2) return NULL;

    mesh_t* mesh = (mesh_t*)malloc(sizeof(mesh_t));
    if (!mesh) return NULL;

    mesh->vertex_count = segments * (rings - 1) + 2;
    mesh->face_count = segments * rings * 2;
    
    mesh->vertices = (vec3_t*)malloc(mesh->vertex_count * sizeof(vec3_t));
    mesh->faces = (int*)malloc(mesh->face_count * 3 * sizeof(int));
    mesh->normals = (vec3_t*)malloc(mesh->vertex_count * sizeof(vec3_t)); // Allocate normals

    if (!mesh->vertices || !mesh->faces || !mesh->normals) {
        if(mesh->vertices) free(mesh->vertices);
        if(mesh->faces) free(mesh->faces);
        if(mesh->normals) free(mesh->normals);
        free(mesh);
        return NULL;
    }
    
    // Top pole
    mesh->vertices[0] = (vec3_t){0, 0, 0.5f};
    
    int v_idx = 1;
    for (int i = 0; i < rings - 1; i++) { // From top ring to bottom ring
        float phi = 3.14159265f * (float)(i + 1) / (float)rings;
        float z_pos = 0.5f * cosf(phi);
        float ring_radius = 0.5f * sinf(phi);
        for (int j = 0; j < segments; j++) {
            float theta = 2.0f * 3.14159265f * (float)j / (float)segments;
            float x_pos = ring_radius * cosf(theta);
            float y_pos = ring_radius * sinf(theta);
            mesh->vertices[v_idx++] = (vec3_t){x_pos, y_pos, z_pos};
        }
    }
    
    // Bottom pole
    mesh->vertices[v_idx] = (vec3_t){0, 0, -0.5f};
    int bottom_pole_idx = v_idx;

    // --- NEW: Specialized Normal Calculation for Sphere ---
    for (int i = 0; i < mesh->vertex_count; i++) {
        mesh->normals[i] = vec3_normalize(mesh->vertices[i]);
    }

    int f_idx = 0;
    // Top cap fan
    for (int i = 0; i < segments; i++) {
        int v1 = 1 + i;
        int v2 = 1 + (i + 1) % segments;
        mesh->faces[f_idx++] = 0;
        mesh->faces[f_idx++] = v2;
        mesh->faces[f_idx++] = v1;
    }

    // Middle rings
    for (int i = 0; i < rings - 2; i++) {
        int ring_start_curr = 1 + i * segments;
        int ring_start_next = 1 + (i + 1) * segments;
        for (int j = 0; j < segments; j++) {
            int v1 = ring_start_curr + j;
            int v2 = ring_start_curr + (j + 1) % segments;
            int v3 = ring_start_next + j;
            int v4 = ring_start_next + (j + 1) % segments;
            
            mesh->faces[f_idx++] = v1;
            mesh->faces[f_idx++] = v3;
            mesh->faces[f_idx++] = v4;

            mesh->faces[f_idx++] = v1;
            mesh->faces[f_idx++] = v4;
            mesh->faces[f_idx++] = v2;
        }
    }

    // Bottom cap fan
    int last_ring_start = 1 + (rings - 2) * segments;
    for (int i = 0; i < segments; i++) {
        int v1 = last_ring_start + i;
        int v2 = last_ring_start + (i + 1) % segments;
        mesh->faces[f_idx++] = bottom_pole_idx;
        mesh->faces[f_idx++] = v1;
        mesh->faces[f_idx++] = v2;
    }
    
    // Invert all face windings at once to ensure they point outwards
    for (int i = 0; i < f_idx / 3; i++) {
        int temp = mesh->faces[i * 3 + 1];
        mesh->faces[i * 3 + 1] = mesh->faces[i * 3 + 2];
        mesh->faces[i * 3 + 2] = temp;
    }

    // We no longer call the generic normal calculation function for the sphere
    // mesh_calculate_normals(mesh); 

    return mesh;
}
mesh_t* create_player_spawn_mesh(void) {
    mesh_t* mesh = (mesh_t*)malloc(sizeof(mesh_t));
    if (!mesh) return NULL;
    mesh->vertex_count = 5;
    mesh->vertices = (vec3_t*)malloc(mesh->vertex_count * sizeof(vec3_t));
    // Base vertices on the XY plane (at Z = 0)
    mesh->vertices[0] = (vec3_t){-0.4f, -0.4f, 0.0f};
    mesh->vertices[1] = (vec3_t){ 0.4f, -0.4f, 0.0f};
    mesh->vertices[2] = (vec3_t){ 0.4f,  0.4f, 0.0f};
    mesh->vertices[3] = (vec3_t){-0.4f,  0.4f, 0.0f};
    // Apex vertex (height is along the Z axis)
    mesh->vertices[4] = (vec3_t){ 0.0f,  0.0f,  1.5f}; // Taller than a regular pyramid

    mesh->face_count = 6;
    mesh->faces = (int*)malloc(mesh->face_count * 3 * sizeof(int));
    int i=0;

    // Base faces (winding order is clockwise so normal points down, -Z)
    mesh->faces[i++]=0; mesh->faces[i++]=2; mesh->faces[i++]=1;
    mesh->faces[i++]=0; mesh->faces[i++]=3; mesh->faces[i++]=2;

    // Side faces (winding order is base-edge -> apex so normal points outwards)
    mesh->faces[i++]=0; mesh->faces[i++]=1; mesh->faces[i++]=4;
    mesh->faces[i++]=1; mesh->faces[i++]=2; mesh->faces[i++]=4;
    mesh->faces[i++]=2; mesh->faces[i++]=3; mesh->faces[i++]=4;
    mesh->faces[i++]=3; mesh->faces[i++]=0; mesh->faces[i++]=4;

    mesh->normals = NULL;
    mesh_calculate_normals(mesh);
    return mesh;
}
mesh_t* create_pyramid_mesh(void) {
    mesh_t* mesh = (mesh_t*)malloc(sizeof(mesh_t));
    if (!mesh) return NULL;
    mesh->vertex_count = 5;
    mesh->vertices = (vec3_t*)malloc(mesh->vertex_count * sizeof(vec3_t));
    // Base vertices on the XY plane (at Z = -0.5)
    mesh->vertices[0] = (vec3_t){-0.5f, -0.5f, -0.5f};
    mesh->vertices[1] = (vec3_t){ 0.5f, -0.5f, -0.5f};
    mesh->vertices[2] = (vec3_t){ 0.5f,  0.5f, -0.5f};
    mesh->vertices[3] = (vec3_t){-0.5f,  0.5f, -0.5f};
    // Apex vertex (height is along the Z axis)
    mesh->vertices[4] = (vec3_t){ 0.0f,  0.0f,  0.5f};
    
    mesh->face_count = 6;
    mesh->faces = (int*)malloc(mesh->face_count * 3 * sizeof(int));
    int i=0;
    
    // Base faces (winding order is clockwise so normal points down, -Z)
    mesh->faces[i++]=0; mesh->faces[i++]=2; mesh->faces[i++]=1;
    mesh->faces[i++]=0; mesh->faces[i++]=3; mesh->faces[i++]=2;
    
    // Side faces (winding order is base-edge -> apex so normal points outwards)
    mesh->faces[i++]=0; mesh->faces[i++]=1; mesh->faces[i++]=4;
    mesh->faces[i++]=1; mesh->faces[i++]=2; mesh->faces[i++]=4;
    mesh->faces[i++]=2; mesh->faces[i++]=3; mesh->faces[i++]=4;
    mesh->faces[i++]=3; mesh->faces[i++]=0; mesh->faces[i++]=4;
    
    mesh->normals = NULL;
    mesh_calculate_normals(mesh);
    return mesh;
}
void destroy_mesh_data(mesh_t* mesh) {
    if (!mesh) return;
    if (mesh->vertices) free(mesh->vertices);
    if (mesh->faces) free(mesh->faces);
    if (mesh->normals) free(mesh->normals);
    free(mesh);
}
void destroy_and_apply_coord_edit() {
    if (!g_hEdit) return;

    char buffer[32];
    GetWindowText(g_hEdit, buffer, sizeof(buffer));
    float new_value = (float)atof(buffer);

    // Check if anything is selected and we are in a valid editing state
    if (g_selected_objects.count > 0 && g_editing_coord_axis != -1) {
        
        // --- Position / Vertex Editing ---
        if (g_editing_coord_axis <= 2) {
            if (g_selected_objects.count == 1) { // Position editing requires a single selection
                scene_object_t* object = g_scene.objects[g_selected_objects.items[0]];
                vec3_t* target_pos = NULL;
                int recalculate_normals = 0; // Flag to see if we need to update normals

                if (g_current_mode == MODE_OBJECT) {
                    target_pos = &object->position;
                } else if (g_current_mode == MODE_EDIT && g_edit_mode_component == EDIT_VERTICES && g_selected_components.count == 1) {
                    int selected_vert_idx = g_selected_components.items[0];
                    target_pos = &object->mesh->vertices[selected_vert_idx];
                    recalculate_normals = 1; // Mark for recalculation
                }

                if (target_pos) {
                    if (g_editing_coord_axis == 0) target_pos->x = new_value;
                    else if (g_editing_coord_axis == 1) target_pos->y = new_value;
                    else if (g_editing_coord_axis == 2) target_pos->z = new_value;
                }
                
                // --- NEW: Recalculate normals if a vertex was moved ---
                if (recalculate_normals) {
                    mesh_calculate_normals(object->mesh);
                }
            }
        }
        // --- Material Property Editing ---
        else if (g_editing_coord_axis == 3) { // Specular Intensity
            for(int i = 0; i < g_selected_objects.count; i++) {
                scene_object_t* obj = g_scene.objects[g_selected_objects.items[i]];
                if (!obj->light_properties) obj->material.specular_intensity = new_value;
            }
        }
        else if (g_editing_coord_axis == 4) { // Shininess
            for(int i = 0; i < g_selected_objects.count; i++) {
                scene_object_t* obj = g_scene.objects[g_selected_objects.items[i]];
                if (!obj->light_properties) obj->material.shininess = new_value;
            }
        }
    }

    DestroyWindow(g_hEdit);
    g_hEdit = NULL;
    g_editing_coord_axis = -1;
}
void draw_mode_ui(HDC hdc) {
    SetBkMode(hdc, TRANSPARENT);

    const char* labels[] = {"Scene Mode", "Model Mode"};
    SIZE text_sizes[2];
    GetTextExtentPoint32(hdc, labels[0], strlen(labels[0]), &text_sizes[0]);
    GetTextExtentPoint32(hdc, labels[1], strlen(labels[1]), &text_sizes[1]);

    int total_width = text_sizes[0].cx + text_sizes[1].cx + 20;
    int x_offset = (g_render_width - total_width) / 2;
    int y_offset = 10;

    for (int i = 0; i < 2; i++) {
        if (i == g_current_editor_mode) {
            SetTextColor(hdc, 0x00FFA500);
        } else {
            SetTextColor(hdc, 0x00FFFFFF);
        }

        TextOut(hdc, x_offset, y_offset, labels[i], strlen(labels[i]));

        g_mode_rects[i].left = x_offset;
        g_mode_rects[i].top = y_offset;
        g_mode_rects[i].right = x_offset + text_sizes[i].cx;
        g_mode_rects[i].bottom = y_offset + text_sizes[i].cy;

        x_offset += text_sizes[i].cx + 20;
    }
}
void draw_tool_ui(HDC hdc) {
    SetBkMode(hdc, TRANSPARENT);

    int x_offset = g_shading_rects[2].right + 30;
    int y_offset = g_render_height - 30;

    const char* labels[] = {"Select", "Draw Face"};

    for (int i = 0; i < 2; i++) {
        if (i == g_current_tool) {
            SetTextColor(hdc, 0x00FFA500);
        } else {
            SetTextColor(hdc, 0x00FFFFFF);
        }

        TextOut(hdc, x_offset, y_offset, labels[i], strlen(labels[i]));

        SIZE text_size;
        GetTextExtentPoint32(hdc, labels[i], strlen(labels[i]), &text_size);
        g_tool_rects[i].left = x_offset;
        g_tool_rects[i].top = y_offset;
        g_tool_rects[i].right = x_offset + text_size.cx;
        g_tool_rects[i].bottom = y_offset + text_size.cy;

        x_offset += text_size.cx + 15;
    }
}
void draw_shading_ui(HDC hdc) {
    SetBkMode(hdc, TRANSPARENT);

    int x_offset = 10;
    int y_offset = g_render_height - 30;

    const char* labels[] = {"Solid", "Smooth", "Wireframe"};

    for (int i = 0; i < 3; i++) {
        if (i == g_shading_mode) {
            SetTextColor(hdc, 0x00FFA500);
        } else {
            SetTextColor(hdc, 0x00FFFFFF);
        }

        TextOut(hdc, x_offset, y_offset, labels[i], strlen(labels[i]));

        SIZE text_size;
        GetTextExtentPoint32(hdc, labels[i], strlen(labels[i]), &text_size);
        g_shading_rects[i].left = x_offset;
        g_shading_rects[i].top = y_offset;
        g_shading_rects[i].right = x_offset + text_size.cx;
        g_shading_rects[i].bottom = y_offset + text_size.cy;

        x_offset += text_size.cx + 15;
    }
}
void draw_scene_outliner(HDC hdc) {
    if (g_current_editor_mode != EDITOR_SCENE) {
        return;
    }

    // --- MODIFIED: Use render dimensions for layout ---
    HBRUSH hBrush = CreateSolidBrush(0x00404040);
    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hbmMem = CreateCompatibleBitmap(hdc, g_render_width, g_render_height);
    HANDLE hOld = SelectObject(hdcMem, hbmMem);

    BitBlt(hdcMem, 0, 0, g_render_width, g_render_height, hdc, 0, 0, SRCCOPY);
    int panel_x = g_render_width - 210;
    RECT panel_rect = { panel_x, 0, g_render_width, g_render_height };
    FillRect(hdcMem, &panel_rect, hBrush);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 128, 0 };
    AlphaBlend(hdc, panel_x, 0, g_render_width - panel_x, g_render_height, hdcMem, panel_x, 0, g_render_width - panel_x, g_render_height, bf);
    
    SelectObject(hdcMem, hOld);
    DeleteObject(hbmMem);
    DeleteDC(hdcMem);
    DeleteObject(hBrush);

    SetBkMode(hdc, TRANSPARENT);
    int y_offset = 10;

    for (int i = 0; i < g_scene.object_count; i++) {
        if (g_scene.objects[i]->parent_index == -1) {
            draw_outliner_object_recursive(hdc, i, 0, &y_offset);
        }
    }
}
void draw_outliner_object_recursive(HDC hdc, int object_index, int depth, int* y_offset) {
    if (object_index < 0 || object_index >= g_scene.object_count) return;

    scene_object_t* obj = g_scene.objects[object_index];
    // --- MODIFIED: Use render width for layout ---
    int x_offset = g_render_width - 200 + (depth * 15);

    if (selection_contains(&g_selected_objects, object_index)) {
        SetTextColor(hdc, 0x00FFA500);
    } else {
        SetTextColor(hdc, 0x00FFFFFF);
    }

    char display_name[128];
    if (g_current_editor_mode == EDITOR_SCENE && obj->is_static) {
        sprintf_s(display_name, sizeof(display_name), "%s [S]", obj->name);
    } else {
        strcpy_s(display_name, sizeof(display_name), obj->name);
    }
    
    int name_len = strlen(display_name);
    TextOut(hdc, x_offset, *y_offset, display_name, name_len);

    SIZE text_size;
    GetTextExtentPoint32(hdc, display_name, name_len, &text_size);
    obj->ui_outliner_rect.left = x_offset;
    obj->ui_outliner_rect.top = *y_offset;
    obj->ui_outliner_rect.right = x_offset + text_size.cx;
    obj->ui_outliner_rect.bottom = *y_offset + text_size.cy;
    
    *y_offset += 20;

    for (int i = 0; i < obj->child_count; i++) {
        draw_outliner_object_recursive(hdc, obj->children[i], depth + 1, y_offset);
    }
}
void draw_sky_ui(HDC hdc) {
    int x_offset = 10;
    // Position it below the object color and material UI
    int y_offset = 10 + (6 * 20); // 3 lines coords + 1 color + 2 material

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, 0x00FFFFFF);

    char text_buffer[] = "Sky:";
    TextOut(hdc, x_offset, y_offset, text_buffer, strlen(text_buffer));
    
    SIZE text_size;
    GetTextExtentPoint32(hdc, text_buffer, strlen(text_buffer), &text_size);

    g_sky_color_swatch_rect.left = x_offset + text_size.cx + 5;
    g_sky_color_swatch_rect.top = y_offset;
    g_sky_color_swatch_rect.right = g_sky_color_swatch_rect.left + 50;
    g_sky_color_swatch_rect.bottom = y_offset + text_size.cy;

    uint8_t r = (uint8_t)(g_sky_color.x * 255.0f);
    uint8_t g = (uint8_t)(g_sky_color.y * 255.0f);
    uint8_t b = (uint8_t)(g_sky_color.z * 255.0f);
    HBRUSH hBrush = CreateSolidBrush(RGB(r, g, b));

    FillRect(hdc, &g_sky_color_swatch_rect, hBrush);

    DeleteObject(hBrush);
}
void draw_material_ui(HDC hdc) {
    // Only draw if one or more mesh objects are selected
    if (g_selected_objects.count == 0) {
        g_material_rects[0].left = -1; // Invalidate rects
        return;
    }
    // Check if the primary selection is a mesh object
    scene_object_t* object = g_scene.objects[g_selected_objects.items[0]];
    if (object->light_properties) {
        g_material_rects[0].left = -1;
        return;
    }
    
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, 0x00FFFFFF);

    char text_buffer[64];
    int x_offset = 10;
    // Position below the Color UI
    int y_offset = 10 + (4 * 20); 

    const char* labels[] = {"Specular: ", "Shininess: "};
    float values[] = {object->material.specular_intensity, object->material.shininess};

    for (int i = 0; i < 2; i++) {
        // g_editing_coord_axis 3=Specular, 4=Shininess
        if (g_hEdit && g_editing_coord_axis == (i + 3)) {
            // Intentionally blank. We skip drawing the text label,
            // but we MUST still increment y_offset below.
        } else {
            sprintf_s(text_buffer, sizeof(text_buffer), "%s%.2f", labels[i], values[i]);
            TextOut(hdc, x_offset, y_offset, text_buffer, strlen(text_buffer));
            
            SIZE text_size;
            GetTextExtentPoint32(hdc, text_buffer, strlen(text_buffer), &text_size);
            g_material_rects[i].left = x_offset;
            g_material_rects[i].top = y_offset;
            g_material_rects[i].right = x_offset + text_size.cx;
            g_material_rects[i].bottom = y_offset + text_size.cy;
        }
        
        y_offset += 20; // This now runs every loop, preserving the layout.
    }
}
void draw_color_ui(HDC hdc) {
    // Only draw if one or more objects are selected
    if (g_selected_objects.count == 0) {
        g_color_swatch_rect.left = -1; // Invalidate rect
        return;
    }

    // --- NEW: Context-aware color source ---
    scene_object_t* object = g_scene.objects[g_selected_objects.items[0]];
    vec3_t color;
    if (object->light_properties) {
        // If it's a light, use the light's emission color
        color = object->light_properties->color;
    } else {
        // Otherwise, use the material's diffuse color
        color = object->material.diffuse_color;
    }
    // --- END NEW ---

    int x_offset = 10;
    // Position it below the coordinate UI
    int y_offset = 10 + (3 * 20); // 3 lines of coords, 20px height each

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, 0x00FFFFFF);

    // Draw the "Color:" label
    char text_buffer[] = "Color:";
    TextOut(hdc, x_offset, y_offset, text_buffer, strlen(text_buffer));
    
    SIZE text_size;
    GetTextExtentPoint32(hdc, text_buffer, strlen(text_buffer), &text_size);

    // Calculate the position for the color swatch rectangle
    g_color_swatch_rect.left = x_offset + text_size.cx + 5; // 5px padding
    g_color_swatch_rect.top = y_offset;
    g_color_swatch_rect.right = g_color_swatch_rect.left + 50; // 50px wide swatch
    g_color_swatch_rect.bottom = y_offset + text_size.cy;

    // Create a brush with the determined color
    uint8_t r = (uint8_t)(color.x * 255.0f);
    uint8_t g = (uint8_t)(color.y * 255.0f);
    uint8_t b = (uint8_t)(color.z * 255.0f);
    HBRUSH hBrush = CreateSolidBrush(RGB(r, g, b));

    // Draw the filled rectangle
    FillRect(hdc, &g_color_swatch_rect, hBrush);

    // Clean up the GDI brush
    DeleteObject(hBrush);
}
void draw_coordinate_ui(HDC hdc) {
    // Only draw if exactly one object is selected.
    if (g_selected_objects.count != 1) {
        g_coord_rects[0].left = -1; // Invalidate rects
        return;
    }

    vec3_t display_pos;
    int is_valid_component = 0;

    int selected_obj_idx = g_selected_objects.items[0];
    scene_object_t* object = g_scene.objects[selected_obj_idx];
    
    if (g_current_mode == MODE_OBJECT) {
        display_pos = object->position;
        is_valid_component = 1;
    } else if (g_current_mode == MODE_EDIT) {
        // In edit mode, only display if exactly one vertex is selected.
        if (g_edit_mode_component == EDIT_VERTICES && g_selected_components.count == 1) {
            int selected_vert_idx = g_selected_components.items[0];
            display_pos = object->mesh->vertices[selected_vert_idx];
            is_valid_component = 1;
        }
    }

    if (!is_valid_component) {
        g_coord_rects[0].left = -1;
        return;
    }

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, 0x00FFFFFF);

    char text_buffer[64];
    int x_offset = 10;
    int y_offset = 10;
    
    const char* labels[] = {"X: ", "Y: ", "Z: "};
    float* values[] = {&display_pos.x, &display_pos.y, &display_pos.z};

    for (int i = 0; i < 3; i++) {
        if (g_hEdit && g_editing_coord_axis == i) {
            // This space is intentionally left blank.
            // We skip drawing text, but we will still increment y_offset below.
        } else {
            sprintf_s(text_buffer, sizeof(text_buffer), "%s%.3f", labels[i], *values[i]);
            TextOut(hdc, x_offset, y_offset, text_buffer, strlen(text_buffer));
            SIZE text_size;
            GetTextExtentPoint32(hdc, text_buffer, strlen(text_buffer), &text_size);
            g_coord_rects[i].left = x_offset;
            g_coord_rects[i].top = y_offset;
            g_coord_rects[i].right = x_offset + text_size.cx;
            g_coord_rects[i].bottom = y_offset + text_size.cy;
        }
        y_offset += 20; // This must run every iteration to keep spacing consistent
    }
}
void trigger_load_scene_dialog(void) {
    OPENFILENAME ofn = {0};
    char szFile[260] = {0};

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_window_handle;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Scene Files\0*.scene\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileName(&ofn) == TRUE) {
        if (scene_load_from_file(&g_scene, ofn.lpstrFile)) {
            // After successfully loading, clear all selections as they are now invalid
            selection_clear(&g_selected_objects);
            selection_clear(&g_selected_components);
            // Reset to object mode
            g_current_mode = MODE_OBJECT;
        }
    }
}
void trigger_export_selection_dialog(void) {
    if (g_selected_objects.count == 0) {
        MessageBox(g_window_handle, "Please select at least one mesh object to export.", "Export Error", MB_OK | MB_ICONWARNING);
        return;
    }

    // --- 1. Calculate the center of the selection to use as the new pivot ---
    vec3_t center = {0, 0, 0};
    int mesh_object_count = 0;
    for (int i = 0; i < g_selected_objects.count; i++) {
        scene_object_t* obj = g_scene.objects[g_selected_objects.items[i]];
        if (obj->mesh) {
            mat4_t world_transform = mat4_get_world_transform(&g_scene, g_selected_objects.items[i]);
            center.x += world_transform.m[0][3];
            center.y += world_transform.m[1][3];
            center.z += world_transform.m[2][3];
            mesh_object_count++;
        }
    }

    if (mesh_object_count == 0) {
        MessageBox(g_window_handle, "The selection contains no mesh objects to export.", "Export Error", MB_OK | MB_ICONWARNING);
        return;
    }
    center = vec3_scale(center, 1.0f / (float)mesh_object_count);


    // --- 2. Create a new, empty mesh to hold the combined result ---
    mesh_t* combined_mesh = (mesh_t*)calloc(1, sizeof(mesh_t));
    if (!combined_mesh) return;

    // --- 3. Iterate through selected objects and combine their meshes ---
    for (int i = 0; i < g_selected_objects.count; i++) {
        scene_object_t* source_obj = g_scene.objects[g_selected_objects.items[i]];
        if (!source_obj->mesh) continue; 

        mat4_t world_transform = mat4_get_world_transform(&g_scene, g_selected_objects.items[i]);
        int vertex_offset = combined_mesh->vertex_count;

        // --- Append vertices, making them relative to the new pivot ---
        int new_total_verts = combined_mesh->vertex_count + source_obj->mesh->vertex_count;
        combined_mesh->vertices = (vec3_t*)realloc(combined_mesh->vertices, new_total_verts * sizeof(vec3_t));
        
        for (int v = 0; v < source_obj->mesh->vertex_count; v++) {
            vec4_t local_pos = {source_obj->mesh->vertices[v].x, source_obj->mesh->vertices[v].y, source_obj->mesh->vertices[v].z, 1.0f};
            vec4_t world_pos = mat4_mul_vec4(world_transform, local_pos);
            
            // Subtract the pivot to make the vertex local to the new model's origin
            vec3_t final_pos = vec3_sub((vec3_t){world_pos.x, world_pos.y, world_pos.z}, center);

            combined_mesh->vertices[vertex_offset + v] = final_pos;
        }
        
        // --- Append faces, re-indexing them as we go ---
        int new_total_faces = combined_mesh->face_count + source_obj->mesh->face_count;
        combined_mesh->faces = (int*)realloc(combined_mesh->faces, new_total_faces * 3 * sizeof(int));

        for (int f = 0; f < source_obj->mesh->face_count; f++) {
            int current_face_idx = (combined_mesh->face_count + f) * 3;
            combined_mesh->faces[current_face_idx + 0] = source_obj->mesh->faces[f*3 + 0] + vertex_offset;
            combined_mesh->faces[current_face_idx + 1] = source_obj->mesh->faces[f*3 + 1] + vertex_offset;
            combined_mesh->faces[current_face_idx + 2] = source_obj->mesh->faces[f*3 + 2] + vertex_offset;
        }

        combined_mesh->vertex_count = new_total_verts;
        combined_mesh->face_count = new_total_faces;
    }

    // --- 4. Open "Save As" dialog and save the new mesh ---
    if (combined_mesh->vertex_count > 0) {
        OPENFILENAME ofn = {0};
        char szFile[260] = {0};

        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = g_window_handle;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = "Model Files\0*.model\0All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
        ofn.lpstrDefExt = "model";

        if (GetSaveFileName(&ofn) == TRUE) {
            scene_object_t temp_obj = { .mesh = combined_mesh };
            model_save_to_file(&temp_obj, ofn.lpstrFile);
        }
    }

    // --- 5. Clean up the temporary combined mesh ---
    destroy_mesh_data(combined_mesh);
}
void trigger_save_model_dialog(void) {
    if (g_selected_objects.count != 1) {
        MessageBox(g_window_handle, "Please select exactly one object to save as a model.", "Save Error", MB_OK | MB_ICONWARNING);
        return;
    }

    OPENFILENAME ofn = {0};
    char szFile[260] = {0}; // buffer for file name

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_window_handle;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Model Files\0*.model\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = "model";

    // Display the Save As dialog box.
    if (GetSaveFileName(&ofn) == TRUE) {
        int selected_object_index = g_selected_objects.items[0];
        model_save_to_file(g_scene.objects[selected_object_index], ofn.lpstrFile);
    }
}
void trigger_save_scene_dialog(void) {
    OPENFILENAME ofn = {0};
    char szFile[260] = {0}; // buffer for file name

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_window_handle;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Scene Files\0*.scene\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = "scene";

    // Display the Save As dialog box.
    if (GetSaveFileName(&ofn) == TRUE) {
        scene_save_to_file(&g_scene, ofn.lpstrFile);
    }
}
int compare_ints_desc(const void* a, const void* b) {
    int int_a = *((int*)a);
    int int_b = *((int*)b);
    if (int_a < int_b) return 1;
    if (int_a > int_b) return -1;
    return 0;
}
// --- Main Entry Point ---
int WINAPI WinMain(HINSTANCE instance, HINSTANCE prev_instance, LPSTR cmd_line, int cmd_show) {
    WNDCLASS window_class = {0};
    window_class.style = CS_HREDRAW | CS_VREDRAW; window_class.lpfnWndProc = window_callback;
    window_class.hInstance = instance; window_class.lpszClassName = "C_3D_Engine_WindowClass";
    window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
    if (!RegisterClass(&window_class)) return 0;

    g_window_width = 800; g_window_height = 600;

    g_render_width = 1280;
    g_render_height = 720;
    
    g_window_handle = CreateWindowEx(0, window_class.lpszClassName, "My C 3D Engine", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, g_window_width, g_window_height, NULL, NULL, instance, NULL);
    if (g_window_handle == NULL) return 0;

    scene_init(&g_scene);
    selection_init(&g_selected_objects);
    selection_init(&g_selected_components);
    g_cube_mesh_data = create_cube_mesh();
    g_pyramid_mesh_data = create_pyramid_mesh();
    g_sphere_mesh_data = create_sphere_mesh(16, 8);
    g_vertex_mesh_data = create_vertex_mesh();
    g_edge_mesh_data = create_edge_mesh();
    g_face_mesh_data = create_face_mesh();
    g_player_spawn_mesh_data = create_player_spawn_mesh(); // <-- ADD THIS LINE
    
    scene_add_object(&g_scene, g_cube_mesh_data, (vec3_t){-1.0f, 0.0f, 0.0f});
    scene_add_object(&g_scene, g_pyramid_mesh_data, (vec3_t){1.0f, 0.0f, 0.0f});

    int running = 1;
    while (running) {
        MSG message;
        while (PeekMessage(&message, NULL, 0, 0, PM_REMOVE)) { if (message.message == WM_QUIT) running = 0; TranslateMessage(&message); DispatchMessage(&message); }
        
        render_frame();
        
        HDC window_dc = GetDC(g_window_handle);
        HDC memory_dc = CreateCompatibleDC(window_dc);
        HBITMAP old_bitmap = (HBITMAP)SelectObject(memory_dc, g_framebuffer_bitmap);

        if (g_current_transform_mode != TRANSFORM_NONE && g_transform_axis_is_locked) {
            vec3_t offset;
            offset.x = g_camera_distance * cosf(g_camera_pitch) * cosf(g_camera_yaw);
            offset.y = g_camera_distance * cosf(g_camera_pitch) * sinf(g_camera_yaw);
            offset.z = g_camera_distance * sinf(g_camera_pitch);
            vec3_t camera_pos = vec3_add(g_camera_target, offset);
            vec3_t up_vector = {0, 0, 1}; if (fabs(sinf(g_camera_pitch)) > 0.999f) up_vector = (vec3_t){0, 1, 0}; 
            mat4_t view_matrix = mat4_look_at(camera_pos, g_camera_target, up_vector);
            mat4_t projection_matrix;
            float aspect_ratio = (float)g_render_width / (float)g_render_height;
            if (g_is_orthographic) {
                float ortho_height = g_camera_distance; float ortho_width = ortho_height * aspect_ratio;
                projection_matrix = mat4_orthographic(-ortho_width/2.0f, ortho_width/2.0f, -ortho_height/2.0f, ortho_height/2.0f, 0.1f, 100.0f);
            } else {
                projection_matrix = mat4_perspective(3.14159f/4.0f, aspect_ratio, 0.1f, 100.0f);
            }
            mat4_t vp_matrix = mat4_mul_mat4(projection_matrix, view_matrix);

            vec3_t center = get_selection_world_center();
            float line_length = 1000.0f;
            vec3_t p1_world = vec3_sub(center, vec3_scale(g_transform_axis, line_length));
            vec3_t p2_world = vec3_add(center, vec3_scale(g_transform_axis, line_length));

            vec4_t p1_clip = mat4_mul_vec4(vp_matrix, (vec4_t){p1_world.x, p1_world.y, p1_world.z, 1.0f});
            vec4_t p2_clip = mat4_mul_vec4(vp_matrix, (vec4_t){p2_world.x, p2_world.y, p2_world.z, 1.0f});

            if (p1_clip.w > 0 && p2_clip.w > 0) {
                int sx1 = (int)((p1_clip.x / p1_clip.w + 1.0f) * 0.5f * g_render_width);
                int sy1 = (int)((1.0f - p1_clip.y / p1_clip.w) * 0.5f * g_render_height);
                int sx2 = (int)((p2_clip.x / p2_clip.w + 1.0f) * 0.5f * g_render_width);
                int sy2 = (int)((1.0f - p2_clip.y / p2_clip.w) * 0.5f * g_render_height);

                uint32_t axis_color = 0xFFFFFFFF;
                if (g_transform_axis.x > 0.9f) axis_color = 0x000000FF;
                else if (g_transform_axis.y > 0.9f) axis_color = 0x0000FF00;
                else if (g_transform_axis.z > 0.9f) axis_color = 0x00FF0000;
                
                HPEN hPen = CreatePen(PS_DOT, 1, axis_color);
                HGDIOBJ hOldPen = SelectObject(memory_dc, hPen);
                MoveToEx(memory_dc, sx1, sy1, NULL);
                LineTo(memory_dc, sx2, sy2);
                SelectObject(memory_dc, hOldPen);
                DeleteObject(hPen);
            }
        }

        draw_scene_outliner(memory_dc);
        draw_coordinate_ui(memory_dc);
        draw_color_ui(memory_dc);
        draw_material_ui(memory_dc);
        draw_sky_ui(memory_dc);
        draw_shading_ui(memory_dc);
        draw_tool_ui(memory_dc);
        draw_mode_ui(memory_dc);
        
        if (g_is_box_selecting) {
            HPEN hPen = CreatePen(PS_DOT, 1, 0x00FFFFFF);
            HGDIOBJ hOldPen = SelectObject(memory_dc, hPen);
            HGDIOBJ hOldBrush = SelectObject(memory_dc, GetStockObject(NULL_BRUSH));
            Rectangle(memory_dc, g_selection_box_rect.left, g_selection_box_rect.top, g_selection_box_rect.right, g_selection_box_rect.bottom);
            SelectObject(memory_dc, hOldBrush);
            SelectObject(memory_dc, hOldPen);
            DeleteObject(hPen);
        }
        
        StretchDIBits(window_dc, 
                      0, 0, g_window_width, g_window_height, 
                      0, 0, g_render_width, g_render_height, 
                      g_framebuffer_memory, &g_framebuffer_info, 
                      DIB_RGB_COLORS, SRCCOPY);

        SelectObject(memory_dc, old_bitmap);
        DeleteDC(memory_dc);
        ReleaseDC(g_window_handle, window_dc);
    }
    return 0;
}
void render_frame() {
    if (!g_framebuffer_memory) return;

    uint8_t r = (uint8_t)(g_sky_color.x * 255.0f);
    uint8_t g = (uint8_t)(g_sky_color.y * 255.0f);
    uint8_t b = (uint8_t)(g_sky_color.z * 255.0f);
    uint32_t clear_color = (r << 16) | (g << 8) | b;

    // --- MODIFIED: Clear buffers based on render resolution ---
    uint32_t* pixel = (uint32_t*)g_framebuffer_memory;
    for (int i = 0; i < g_render_width * g_render_height; ++i) {
        *pixel++ = clear_color;
        g_depth_buffer[i] = FLT_MAX;
    }

    vec3_t offset;
    offset.x = g_camera_distance * cosf(g_camera_pitch) * cosf(g_camera_yaw);
    offset.y = g_camera_distance * cosf(g_camera_pitch) * sinf(g_camera_yaw);
    offset.z = g_camera_distance * sinf(g_camera_pitch);
    vec3_t camera_pos = vec3_add(g_camera_target, offset);

    vec3_t up_vector = {0, 0, 1};
    if (fabs(sinf(g_camera_pitch)) > 0.999f) {
        up_vector = (vec3_t){0, 1, 0}; 
    }
    mat4_t view_matrix = mat4_look_at(camera_pos, g_camera_target, up_vector);
    
    mat4_t projection_matrix;
    float aspect_ratio = (float)g_render_width / (float)g_render_height;

    if (g_is_orthographic) {
        float ortho_height = g_camera_distance; 
        float ortho_width = ortho_height * aspect_ratio;
        projection_matrix = mat4_orthographic(-ortho_width / 2.0f, ortho_width / 2.0f, -ortho_height / 2.0f, ortho_height / 2.0f, 0.1f, 100.0f);
    } else {
        projection_matrix = mat4_perspective(3.14159f / 4.0f, aspect_ratio, 0.1f, 100.0f);
    }

    active_light_t active_lights[MAX_LIGHTS];
    int light_count = 0;
    for (int i = 0; i < g_scene.object_count && light_count < MAX_LIGHTS; i++) {
        scene_object_t* obj = g_scene.objects[i];
        if (obj->light_properties) {
            mat4_t light_transform = mat4_get_world_transform(&g_scene, i);
            active_lights[light_count].position = (vec3_t){light_transform.m[0][3], light_transform.m[1][3], light_transform.m[2][3]};
            active_lights[light_count].properties = obj->light_properties;

            if (obj->light_properties->type == LIGHT_TYPE_SPOT) {
                mat4_t rot_matrix = mat4_mul_mat4(mat4_rotation_z(obj->rotation.z), mat4_mul_mat4(mat4_rotation_y(obj->rotation.y), mat4_rotation_x(obj->rotation.x)));
                vec4_t local_dir = {0, 0, -1, 0}; 
                vec4_t world_dir4 = mat4_mul_vec4(rot_matrix, local_dir);
                active_lights[light_count].direction = vec3_normalize((vec3_t){world_dir4.x, world_dir4.y, world_dir4.z});
            }

            light_count++;
        }
    }

    render_grid(view_matrix, projection_matrix);
    
    for (int i = 0; i < g_scene.object_count; i++) {
        render_object(g_scene.objects[i], i, view_matrix, projection_matrix, camera_pos, active_lights, light_count);
    }
}
void build_specular_table(float shininess) {
    if (shininess == g_current_shininess_in_table) {
        return; // Table is already built for this shininess value
    }
    for (int i = 0; i < SPECULAR_TABLE_SIZE; i++) {
        float cos_angle = (float)i / (float)(SPECULAR_TABLE_SIZE - 1);
        g_specular_lookup_table[i] = powf(cos_angle, shininess);
    }
    g_current_shininess_in_table = shininess;
}
int clip_triangle_against_near_plane(triangle_t* in_tri, triangle_t* out_tri1, triangle_t* out_tri2) {
    vec4_t inside_points[3];  int inside_count = 0;
    vec3_t inside_colors[3];
    vec4_t outside_points[3]; int outside_count = 0;
    vec3_t outside_colors[3];

    // A small epsilon to avoid issues with triangles exactly on the plane
    const float near_plane_w = 0.001f;

    // Categorize each vertex as being inside or outside the near plane
    for (int i = 0; i < 3; i++) {
        if (in_tri->vertices[i].w > near_plane_w) {
            inside_points[inside_count] = in_tri->vertices[i];
            inside_colors[inside_count] = in_tri->colors[i];
            inside_count++;
        } else {
            outside_points[outside_count] = in_tri->vertices[i];
            outside_colors[outside_count] = in_tri->colors[i];
            outside_count++;
        }
    }

    // --- Process based on how many vertices are visible ---

    if (inside_count == 3) {
        // The entire triangle is visible, no clipping needed
        *out_tri1 = *in_tri;
        return 1;
    }

    if (inside_count == 0) {
        // The entire triangle is behind the camera, discard it
        return 0;
    }

    if (inside_count == 1) {
        // The triangle is cut, leaving one smaller triangle
        out_tri1->vertices[0] = inside_points[0];
        out_tri1->colors[0] = inside_colors[0];

        // This triangle has one visible vertex and two outside.
        // We must calculate the two new vertices where the edges
        // cross the near plane.
        for (int i = 0; i < 2; i++) {
            float t = (inside_points[0].w - near_plane_w) / (inside_points[0].w - outside_points[i].w);
            out_tri1->vertices[i + 1].x = inside_points[0].x + t * (outside_points[i].x - inside_points[0].x);
            out_tri1->vertices[i + 1].y = inside_points[0].y + t * (outside_points[i].y - inside_points[0].y);
            out_tri1->vertices[i + 1].z = inside_points[0].z + t * (outside_points[i].z - inside_points[0].z);
            out_tri1->vertices[i + 1].w = inside_points[0].w + t * (outside_points[i].w - outside_points[0].w);

            out_tri1->colors[i + 1].x = inside_colors[0].x + t * (outside_colors[i].x - inside_colors[0].x);
            out_tri1->colors[i + 1].y = inside_colors[0].y + t * (outside_colors[i].y - inside_colors[0].y);
            out_tri1->colors[i + 1].z = inside_colors[0].z + t * (outside_colors[i].z - inside_colors[0].z);
        }
        return 1;
    }

    if (inside_count == 2) {
        // The triangle is cut, leaving a quad which we split into two triangles.
        // First output triangle
        out_tri1->vertices[0] = inside_points[0];
        out_tri1->vertices[1] = inside_points[1];
        out_tri1->colors[0] = inside_colors[0];
        out_tri1->colors[1] = inside_colors[1];

        float t0 = (inside_points[0].w - near_plane_w) / (inside_points[0].w - outside_points[0].w);
        out_tri1->vertices[2].x = inside_points[0].x + t0 * (outside_points[0].x - inside_points[0].x);
        out_tri1->vertices[2].y = inside_points[0].y + t0 * (outside_points[0].y - inside_points[0].y);
        out_tri1->vertices[2].z = inside_points[0].z + t0 * (outside_points[0].z - inside_points[0].z);
        out_tri1->vertices[2].w = inside_points[0].w + t0 * (outside_points[0].w - inside_points[0].w);
        out_tri1->colors[2].x = inside_colors[0].x + t0 * (outside_colors[0].x - inside_colors[0].x);
        out_tri1->colors[2].y = inside_colors[0].y + t0 * (outside_colors[0].y - inside_colors[0].y);
        out_tri1->colors[2].z = inside_colors[0].z + t0 * (outside_colors[0].z - inside_colors[0].z);

        // Second output triangle
        out_tri2->vertices[0] = inside_points[1];
        out_tri2->colors[0] = inside_colors[1];
        out_tri2->vertices[1] = out_tri1->vertices[2]; // Reuse the previously calculated intersection point
        out_tri2->colors[1] = out_tri1->colors[2];

        float t1 = (inside_points[1].w - near_plane_w) / (inside_points[1].w - outside_points[0].w);
        out_tri2->vertices[2].x = inside_points[1].x + t1 * (outside_points[0].x - inside_points[1].x);
        out_tri2->vertices[2].y = inside_points[1].y + t1 * (outside_points[0].y - inside_points[1].y);
        out_tri2->vertices[2].z = inside_points[1].z + t1 * (outside_points[0].z - inside_points[1].z);
        out_tri2->vertices[2].w = inside_points[1].w + t1 * (outside_points[0].w - inside_points[1].w);
        out_tri2->colors[2].x = inside_colors[1].x + t1 * (outside_colors[0].x - inside_colors[1].x);
        out_tri2->colors[2].y = inside_colors[1].y + t1 * (outside_colors[0].y - inside_colors[1].y);
        out_tri2->colors[2].z = inside_colors[1].z + t1 * (outside_colors[0].z - inside_colors[1].z);

        return 2;
    }
    return 0; // Should not happen
}
void render_object(scene_object_t* object, int object_index, mat4_t view_matrix, mat4_t projection_matrix, vec3_t camera_pos, const active_light_t* lights, int light_count) {
    if (object->light_properties) {
        mat4_t model_matrix = mat4_get_world_transform(&g_scene, object_index);
        mat4_t vp_matrix = mat4_mul_mat4(projection_matrix, view_matrix);
        vec3_t pos = { model_matrix.m[0][3], model_matrix.m[1][3], model_matrix.m[2][3] };
        uint32_t gizmo_color = (selection_contains(&g_selected_objects, object_index)) ? 0xFFFFA500 : 0xFFFFFF00;

        float gizmo_size = 0.25f;
        vec3_t points[6] = {
            vec3_add(pos, (vec3_t){-gizmo_size, 0, 0}), vec3_add(pos, (vec3_t){gizmo_size, 0, 0}),
            vec3_add(pos, (vec3_t){0, -gizmo_size, 0}), vec3_add(pos, (vec3_t){0, gizmo_size, 0}),
            vec3_add(pos, (vec3_t){0, 0, -gizmo_size}), vec3_add(pos, (vec3_t){0, 0, gizmo_size})
        };
        for (int i = 0; i < 3; i++) {
            vec4_t p1_clip = mat4_mul_vec4(vp_matrix, (vec4_t){points[i*2].x, points[i*2].y, points[i*2].z, 1.0f});
            vec4_t p2_clip = mat4_mul_vec4(vp_matrix, (vec4_t){points[i*2+1].x, points[i*2+1].y, points[i*2+1].z, 1.0f});
            if (p1_clip.w > 0 && p2_clip.w > 0) {
                 int sx1=(p1_clip.x/p1_clip.w+1)*0.5f*g_render_width, sy1=(1-p1_clip.y/p1_clip.w)*0.5f*g_render_height;
                 int sx2=(p2_clip.x/p2_clip.w+1)*0.5f*g_render_width, sy2=(1-p2_clip.y/p2_clip.w)*0.5f*g_render_height;
                 draw_line(sx1, sy1, p1_clip.z/p1_clip.w, sx2, sy2, p2_clip.z/p2_clip.w, gizmo_color);
            }
        }
        
        if (object->light_properties->type == LIGHT_TYPE_SPOT) {
            float cone_len = 3.0f;
            mat4_t rot_matrix = mat4_mul_mat4(mat4_rotation_z(object->rotation.z), mat4_mul_mat4(mat4_rotation_y(object->rotation.y), mat4_rotation_x(object->rotation.x)));
            vec4_t local_dir = {0, 0, -1, 0};
            vec4_t world_dir4 = mat4_mul_vec4(rot_matrix, local_dir);
            vec3_t world_dir = vec3_normalize((vec3_t){world_dir4.x, world_dir4.y, world_dir4.z});
            vec3_t cone_end = vec3_add(pos, vec3_scale(world_dir, cone_len));
            float radius = cone_len * tanf(object->light_properties->spot_angle / 2.0f);
            vec3_t up_dummy = (fabs(world_dir.z) < 0.99f) ? (vec3_t){0,0,1} : (vec3_t){0,1,0};
            vec3_t right_vec = vec3_normalize(vec3_cross(world_dir, up_dummy));
            vec3_t up_vec = vec3_cross(right_vec, world_dir);
            vec3_t circle_pts[4];
            circle_pts[0] = vec3_add(cone_end, vec3_scale(right_vec, radius));
            circle_pts[1] = vec3_add(cone_end, vec3_scale(up_vec, radius));
            circle_pts[2] = vec3_add(cone_end, vec3_scale(right_vec, -radius));
            circle_pts[3] = vec3_add(cone_end, vec3_scale(up_vec, -radius));
            vec4_t apex_clip = mat4_mul_vec4(vp_matrix, (vec4_t){pos.x, pos.y, pos.z, 1.0f});
            for (int i=0; i<4; i++) {
                vec4_t circle_clip = mat4_mul_vec4(vp_matrix, (vec4_t){circle_pts[i].x, circle_pts[i].y, circle_pts[i].z, 1.0f});
                vec4_t next_circle_clip = mat4_mul_vec4(vp_matrix, (vec4_t){circle_pts[(i+1)%4].x, circle_pts[(i+1)%4].y, circle_pts[(i+1)%4].z, 1.0f});
                if(apex_clip.w > 0 && circle_clip.w > 0 && next_circle_clip.w > 0) {
                    int sx1=(apex_clip.x/apex_clip.w+1)*0.5f*g_render_width, sy1=(1-apex_clip.y/apex_clip.w)*0.5f*g_render_height;
                    int sx2=(circle_clip.x/circle_clip.w+1)*0.5f*g_render_width, sy2=(1-circle_clip.y/circle_clip.w)*0.5f*g_render_height;
                    int sx3=(next_circle_clip.x/next_circle_clip.w+1)*0.5f*g_render_width, sy3=(1-next_circle_clip.y/next_circle_clip.w)*0.5f*g_render_height;
                    draw_line(sx1, sy1, apex_clip.z/apex_clip.w, sx2, sy2, circle_clip.z/circle_clip.w, gizmo_color);
                    draw_line(sx2, sy2, circle_clip.z/circle_clip.w, sx3, sy3, next_circle_clip.z/next_circle_clip.w, gizmo_color);
                }
            }
        }
        return; 
    }
    
    if (!object || !object->mesh || object->mesh->vertex_count == 0) return;

    if (object->mesh->vertex_count > g_vertex_buffer_capacity) {
        g_vertex_buffer_capacity = object->mesh->vertex_count;
        g_clip_coords_buffer = (vec4_t*)realloc(g_clip_coords_buffer, g_vertex_buffer_capacity * sizeof(vec4_t));
        g_colors_buffer = (vec3_t*)realloc(g_colors_buffer, g_vertex_buffer_capacity * sizeof(vec3_t));
        if (!g_clip_coords_buffer || !g_colors_buffer) {
             g_vertex_buffer_capacity = 0;
             return;
        }
    }

    mat4_t model_matrix = mat4_get_world_transform(&g_scene, object_index);
    mat4_t final_transform = mat4_mul_mat4(projection_matrix, mat4_mul_mat4(view_matrix, model_matrix));
    
    int is_object_selected = selection_contains(&g_selected_objects, object_index);
    int use_precomputed_colors = 0;

    if (g_shading_mode == SHADING_SMOOTH && object->mesh->normals && !object->is_player_spawn) { // Player spawn is always solid color
        use_precomputed_colors = 1;
        
        if (object->material.specular_intensity > 0.0f) {
            build_specular_table(object->material.shininess);
        }

        for (int i = 0; i < object->mesh->vertex_count; i++) {
            g_clip_coords_buffer[i] = mat4_mul_vec4(final_transform, (vec4_t){object->mesh->vertices[i].x, object->mesh->vertices[i].y, object->mesh->vertices[i].z, 1.0f});

            vec4_t v_world_4 = mat4_mul_vec4(model_matrix, (vec4_t){object->mesh->vertices[i].x, object->mesh->vertices[i].y, object->mesh->vertices[i].z, 1.0f});
            vec3_t v_world = {v_world_4.x, v_world_4.y, v_world_4.z};
            vec4_t n_world_4 = mat4_mul_vec4(model_matrix, (vec4_t){object->mesh->normals[i].x, object->mesh->normals[i].y, object->mesh->normals[i].z, 0.0f});
            vec3_t n_world = vec3_normalize((vec3_t){n_world_4.x, n_world_4.y, n_world_4.z});
            vec3_t diffuse_sum = {0.1f, 0.1f, 0.1f}, specular_sum = {0,0,0};
            vec3_t view_dir = vec3_normalize(vec3_sub(camera_pos, v_world));
            
            for (int l = 0; l < light_count; l++) {
                light_t* light_prop = lights[l].properties;
                vec3_t to_light = vec3_sub(lights[l].position, v_world);
                float dist_sq = vec3_dot(to_light, to_light);
                if(dist_sq < 1e-6) dist_sq = 1e-6;
                vec3_t light_dir = vec3_normalize(to_light);
                float attenuation = light_prop->intensity / dist_sq;
                float diff_intensity = fmax(vec3_dot(n_world, light_dir), 0.0f);
                if (light_prop->type == LIGHT_TYPE_SPOT) {
                    float theta = vec3_dot(light_dir, vec3_scale(lights[l].direction, -1.0f));
                    float epsilon = cosf(light_prop->spot_angle / 2.0f);
                    if (theta > epsilon) {
                         float falloff_angle = (light_prop->spot_angle / 2.0f) * (1.0f - light_prop->spot_blend);
                         float falloff_cos = cosf(falloff_angle);
                         float spot_effect = (theta - epsilon) / (falloff_cos - epsilon);
                         spot_effect = (spot_effect < 0.0f) ? 0.0f : (spot_effect > 1.0f) ? 1.0f : spot_effect;
                         attenuation *= spot_effect;
                    } else {
                        attenuation = 0;
                    }
                }
                if (attenuation > 0) {
                    diffuse_sum = vec3_add(diffuse_sum, vec3_scale(light_prop->color, diff_intensity * attenuation));
                    if(diff_intensity > 0.0f && object->material.specular_intensity > 0.0f) {
                        vec3_t reflect_dir = vec3_sub(vec3_scale(n_world, 2.0f * vec3_dot(n_world, light_dir)), light_dir);
                        float spec_angle = fmax(vec3_dot(view_dir, reflect_dir), 0.0f);
                        
                        int table_index = (int)(spec_angle * (SPECULAR_TABLE_SIZE - 1));
                        float specular_term = g_specular_lookup_table[table_index];
                        
                        specular_sum = vec3_add(specular_sum, vec3_scale(light_prop->color, specular_term * object->material.specular_intensity * attenuation));
                    }
                }
            }
            g_colors_buffer[i].x = object->material.diffuse_color.x * diffuse_sum.x + specular_sum.x;
            g_colors_buffer[i].y = object->material.diffuse_color.y * diffuse_sum.y + specular_sum.y;
            g_colors_buffer[i].z = object->material.diffuse_color.z * diffuse_sum.z + specular_sum.z;
        }
    } else {
        for (int i = 0; i < object->mesh->vertex_count; i++) {
            g_clip_coords_buffer[i] = mat4_mul_vec4(final_transform, (vec4_t){object->mesh->vertices[i].x, object->mesh->vertices[i].y, object->mesh->vertices[i].z, 1.0f});
        }
    }

    if (object->mesh->face_count > 0) {
        for (int i = 0; i < object->mesh->face_count; ++i) {
            int v_indices[3] = {object->mesh->faces[i*3+0], object->mesh->faces[i*3+1], object->mesh->faces[i*3+2]};
            
            vec4_t v_clip[3] = { g_clip_coords_buffer[v_indices[0]], g_clip_coords_buffer[v_indices[1]], g_clip_coords_buffer[v_indices[2]] };

            if (v_clip[0].w > 0 && v_clip[1].w > 0 && v_clip[2].w > 0) {
                 vec3_t v0_ndc = {v_clip[0].x/v_clip[0].w, v_clip[0].y/v_clip[0].w, v_clip[0].z/v_clip[0].w};
                 vec3_t v1_ndc = {v_clip[1].x/v_clip[1].w, v_clip[1].y/v_clip[1].w, v_clip[1].z/v_clip[1].w};
                 vec3_t v2_ndc = {v_clip[2].x/v_clip[2].w, v_clip[2].y/v_clip[2].w, v_clip[2].z/v_clip[2].w};
                 float signed_area_z = (v1_ndc.x - v0_ndc.x) * (v2_ndc.y - v0_ndc.y) - (v1_ndc.y - v0_ndc.y) * (v2_ndc.x - v0_ndc.x);
                 if (!object->is_double_sided && signed_area_z < 0) {
                     continue;
                 }
            }

            triangle_t original_tri;
            for(int j=0; j<3; j++) {
                original_tri.vertices[j] = v_clip[j];
                if (use_precomputed_colors) {
                    original_tri.colors[j] = g_colors_buffer[v_indices[j]];
                } else {
                    original_tri.colors[j] = object->material.diffuse_color;
                }
            }

            triangle_t clipped_tris[2];
            int num_clipped = clip_triangle_against_near_plane(&original_tri, &clipped_tris[0], &clipped_tris[1]);

            for (int t = 0; t < num_clipped; t++) {
                if (g_shading_mode != SHADING_WIREFRAME) {
                    if (use_precomputed_colors) {
                        draw_gouraud_triangle(clipped_tris[t].vertices[0], clipped_tris[t].vertices[1], clipped_tris[t].vertices[2], clipped_tris[t].colors[0], clipped_tris[t].colors[1], clipped_tris[t].colors[2]);
                    } else {
                        uint8_t r = (uint8_t)(clipped_tris[t].colors[0].x * 255.0f);
                        uint8_t g = (uint8_t)(clipped_tris[t].colors[0].y * 255.0f);
                        uint8_t b = (uint8_t)(clipped_tris[t].colors[0].z * 255.0f);
                        uint32_t face_color = (r << 16) | (g << 8) | b;
                         if (g_current_mode == MODE_EDIT && g_edit_mode_component == EDIT_FACES && is_object_selected && selection_contains(&g_selected_components, i)) { face_color = 0xFFFFA500; }
                        draw_filled_triangle(clipped_tris[t].vertices[0], clipped_tris[t].vertices[1], clipped_tris[t].vertices[2], face_color);
                    }
                }

                vec4_t sp[3];
                for(int j=0; j<3; ++j) {
                    sp[j] = clipped_tris[t].vertices[j];
                    if(sp[j].w != 0) {
                        sp[j].x = (sp[j].x / sp[j].w + 1.0f) * 0.5f * g_render_width;
                        sp[j].y = (1.0f - sp[j].y / sp[j].w) * 0.5f * g_render_height;
                        sp[j].z /= sp[j].w;
                    }
                }
                
                uint32_t wire_color;
                if (object->is_player_spawn) { 
                    wire_color = 0xFFFF0000; // Red
                } else if (object->is_player_model) { // <-- NEW
                    wire_color = 0xFF00FF00; // Bright Green
                } else if (g_current_editor_mode == EDITOR_SCENE && object->is_static) {
                    wire_color = 0xFF606060;
                } else {
                    wire_color = (g_current_mode==MODE_OBJECT)?(is_object_selected?0xFFFF00:0xFFFFFF):(is_object_selected?0xFFFF00:0xFF808080);
                }
                draw_line(sp[0].x,sp[0].y,sp[0].z - 0.002f, sp[1].x,sp[1].y,sp[1].z - 0.002f, wire_color);
                draw_line(sp[1].x,sp[1].y,sp[1].z - 0.002f, sp[2].x,sp[2].y,sp[2].z - 0.002f, wire_color);
                draw_line(sp[2].x,sp[2].y,sp[2].z - 0.002f, sp[0].x,sp[0].y,sp[0].z - 0.002f, wire_color);
            }
        }
    } 
    
    // --- NEW: Draw camera offset gizmo ---
    if (object->is_player_model) {
        vec4_t offset_local = {object->camera_offset.x, object->camera_offset.y, object->camera_offset.z, 1.0f};
        vec4_t offset_clip = mat4_mul_vec4(final_transform, offset_local);

        if (offset_clip.w > 0) {
            float sx = (offset_clip.x / offset_clip.w + 1.0f) * 0.5f * g_render_width;
            float sy = (1.0f - offset_clip.y / offset_clip.w) * 0.5f * g_render_height;
            float sz = offset_clip.z / offset_clip.w;
            
            // Draw a small cyan 'X'
            draw_line(sx - 3, sy - 3, sz - 0.003f, sx + 3, sy + 3, sz - 0.003f, 0xFFFFFF00);
            draw_line(sx - 3, sy + 3, sz - 0.003f, sx + 3, sy - 3, sz - 0.003f, 0xFFFFFF00);
        }
    }
    
    int should_draw_markers = (g_current_mode == MODE_EDIT && is_object_selected) || (object->mesh->face_count == 0 && is_object_selected);
    if (should_draw_markers) {
        for (int i = 0; i < object->mesh->vertex_count; i++) {
            vec4_t v_proj = g_clip_coords_buffer[i];
            if (v_proj.w > 0) {
                float sx = (v_proj.x / v_proj.w + 1.0f) * 0.5f * g_render_width;
                float sy = (1.0f - v_proj.y / v_proj.w) * 0.5f * g_render_height;
                float sz = v_proj.z / v_proj.w;
                uint32_t vertex_color = (g_current_mode == MODE_EDIT && g_edit_mode_component == EDIT_VERTICES && selection_contains(&g_selected_components, i)) ? 0xFFFFA500 : 0xFF00FFFF;
                draw_vertex_marker(sx, sy, sz, vertex_color);
            }
        }
    }
}
void render_grid(mat4_t view_matrix, mat4_t projection_matrix) {
    int grid_size = 10; float half_size = grid_size/2.0f;
    uint32_t grid_color = 0xFF808080, axis_color_y = 0xFF00FF00, axis_color_x = 0xFFFF0000, axis_color_z = 0xFF0000FF;
    mat4_t vp_matrix = mat4_mul_mat4(projection_matrix, view_matrix);
    
    // Draw grid lines on the XY plane (Z=0)
    for (int i = -half_size; i <= half_size; i++) {
        if (i == 0) continue;
        vec4_t p1 = {i, -half_size, 0, 1}, p2 = {i, half_size, 0, 1};
        vec4_t p3 = {-half_size, i, 0, 1}, p4 = {half_size, i, 0, 1};
        
        vec4_t sp[4] = {mat4_mul_vec4(vp_matrix,p1), mat4_mul_vec4(vp_matrix,p2), mat4_mul_vec4(vp_matrix,p3), mat4_mul_vec4(vp_matrix,p4)};
        
        // --- MODIFIED: Use render resolution for screen space transform ---
        for(int j=0;j<4;++j){if(sp[j].w!=0){sp[j].x=(sp[j].x/sp[j].w+1)*0.5f*g_render_width; sp[j].y=(1-sp[j].y/sp[j].w)*0.5f*g_render_height; sp[j].z=sp[j].z/sp[j].w;}}
        
        draw_line(sp[0].x,sp[0].y,sp[0].z, sp[1].x,sp[1].y,sp[1].z, grid_color);
        draw_line(sp[2].x,sp[2].y,sp[2].z, sp[3].x,sp[3].y,sp[3].z, grid_color);
    }
    
    vec4_t x1={-half_size,0,0,1}, x2={half_size,0,0,1};
    vec4_t y1={0,-half_size,0,1}, y2={0,half_size,0,1};
    vec4_t z1={0,0,-half_size,1}, z2={0,0,half_size,1};

    vec4_t axis_p[6] = {mat4_mul_vec4(vp_matrix,x1), mat4_mul_vec4(vp_matrix,x2), mat4_mul_vec4(vp_matrix,y1), mat4_mul_vec4(vp_matrix,y2), mat4_mul_vec4(vp_matrix,z1), mat4_mul_vec4(vp_matrix,z2)};
    
    // --- MODIFIED: Use render resolution for screen space transform ---
    for(int j=0;j<6;++j){if(axis_p[j].w!=0){axis_p[j].x=(axis_p[j].x/axis_p[j].w+1)*0.5f*g_render_width; axis_p[j].y=(1-axis_p[j].y/axis_p[j].w)*0.5f*g_render_height; axis_p[j].z=axis_p[j].z/axis_p[j].w;}}
    
    draw_line(axis_p[0].x,axis_p[0].y,axis_p[0].z, axis_p[1].x,axis_p[1].y,axis_p[1].z, axis_color_x);
    draw_line(axis_p[2].x,axis_p[2].y,axis_p[2].z, axis_p[3].x,axis_p[3].y,axis_p[3].z, axis_color_y);
    draw_line(axis_p[4].x,axis_p[4].y,axis_p[4].z, axis_p[5].x,axis_p[5].y,axis_p[5].z, axis_color_z);
}
vec3_t get_selection_world_center(void) {
    vec3_t center = {0, 0, 0};
    if (g_selected_objects.count == 0) return center;

    if (g_current_mode == MODE_OBJECT) {
        for (int i = 0; i < g_selected_objects.count; i++) {
            mat4_t world_matrix = mat4_get_world_transform(&g_scene, g_selected_objects.items[i]);
            center.x += world_matrix.m[0][3];
            center.y += world_matrix.m[1][3];
            center.z += world_matrix.m[2][3];
        }
        if (g_selected_objects.count > 0) {
            center = vec3_scale(center, 1.0f / g_selected_objects.count);
        }
    } else { // MODE_EDIT
        scene_object_t* obj = g_scene.objects[g_selected_objects.items[0]];
        if (g_selected_components.count == 0) {
             mat4_t world_matrix = mat4_get_world_transform(&g_scene, g_selected_objects.items[0]);
             center.x = world_matrix.m[0][3];
             center.y = world_matrix.m[1][3];
             center.z = world_matrix.m[2][3];
        } else {
            vec3_t local_pivot = get_selection_center(obj);
            mat4_t model_matrix = mat4_get_world_transform(&g_scene, g_selected_objects.items[0]);
            vec4_t world_pivot_4d = mat4_mul_vec4(model_matrix, (vec4_t){local_pivot.x, local_pivot.y, local_pivot.z, 1.0f});
            center = (vec3_t){world_pivot_4d.x, world_pivot_4d.y, world_pivot_4d.z};
        }
    }
    return center;
}
vec3_t get_selection_center(scene_object_t* object) {
    if (!object || g_selected_components.count == 0) {
        return (vec3_t){0, 0, 0};
    }

    vec3_t center = {0, 0, 0};
    int total_verts = 0;

    // To avoid double-counting vertices, we use a map
    int* counted_verts = (int*)calloc(object->mesh->vertex_count, sizeof(int));
    if (!counted_verts) return (vec3_t){0,0,0};

    for (int i = 0; i < g_selected_components.count; i++) {
        int comp_idx = g_selected_components.items[i];
        
        if (g_edit_mode_component == EDIT_VERTICES) {
            if (!counted_verts[comp_idx]) {
                center = vec3_add(center, object->mesh->vertices[comp_idx]);
                total_verts++;
                counted_verts[comp_idx] = 1;
            }
        } else if (g_edit_mode_component == EDIT_EDGES) {
            int v_indices[2] = {(comp_idx >> 16) & 0xFFFF, comp_idx & 0xFFFF};
            for (int v = 0; v < 2; v++) {
                if (!counted_verts[v_indices[v]]) {
                    center = vec3_add(center, object->mesh->vertices[v_indices[v]]);
                    total_verts++;
                    counted_verts[v_indices[v]] = 1;
                }
            }
        } else if (g_edit_mode_component == EDIT_FACES) {
            int v_indices[3] = {object->mesh->faces[comp_idx*3+0], object->mesh->faces[comp_idx*3+1], object->mesh->faces[comp_idx*3+2]};
            for (int v = 0; v < 3; v++) {
                if (!counted_verts[v_indices[v]]) {
                    center = vec3_add(center, object->mesh->vertices[v_indices[v]]);
                    total_verts++;
                    counted_verts[v_indices[v]] = 1;
                }
            }
        }
    }

    free(counted_verts);

    if (total_verts > 0) {
        center.x /= total_verts;
        center.y /= total_verts;
        center.z /= total_verts;
    }

    return center;
}
// --- Picking Functions ---
int ray_intersects_triangle(vec3_t ro,vec3_t rv,vec3_t v0,vec3_t v1,vec3_t v2,float* d){const float EPS=1e-7f;vec3_t e1=vec3_sub(v1,v0),e2=vec3_sub(v2,v0),h=vec3_cross(rv,e2);float a=vec3_dot(e1,h);if(a>-EPS&&a<EPS)return 0;float f=1.f/a;vec3_t s=vec3_sub(ro,v0);float u=f*vec3_dot(s,h);if(u<0.f||u>1.f)return 0;vec3_t q=vec3_cross(s,e1);float v=f*vec3_dot(rv,q);if(v<0.f||u+v>1.f)return 0;float t=f*vec3_dot(e2,q);if(t>EPS){if(d)*d=t;return 1;}return 0;}

int find_clicked_object(int mx, int my) {
    // MODIFIED: Scale mouse from window space to render space
    float scaled_mx = (float)mx * ((float)g_render_width / (float)g_window_width);
    float scaled_my = (float)my * ((float)g_render_height / (float)g_window_height);

    float xn=(2.0f*scaled_mx)/g_render_width-1.0f, yn=1.0f-(2.0f*scaled_my)/g_render_height;
    mat4_t proj=mat4_perspective(3.14159f/4.0f, (float)g_render_width/(float)g_render_height, 0.1f, 100.0f);
    
    vec3_t offset;
    offset.x = g_camera_distance * cosf(g_camera_pitch) * cosf(g_camera_yaw);
    offset.y = g_camera_distance * cosf(g_camera_pitch) * sinf(g_camera_yaw);
    offset.z = g_camera_distance * sinf(g_camera_pitch);
    vec3_t cam_pos = vec3_add(g_camera_target, offset);
    
    vec3_t up_vector = {0, 0, 1};
    if (fabs(sinf(g_camera_pitch)) > 0.999f) {
        up_vector = (vec3_t){0, 1, 0};
    }
    mat4_t view = mat4_look_at(cam_pos, g_camera_target, up_vector);

    mat4_t inv_vp=mat4_inverse(mat4_mul_mat4(proj,view));
    vec4_t near_p_w=mat4_mul_vec4(inv_vp,(vec4_t){xn,yn,-1,1}), far_p_w=mat4_mul_vec4(inv_vp,(vec4_t){xn,yn,1,1});
    if(near_p_w.w!=0){near_p_w.x/=near_p_w.w; near_p_w.y/=near_p_w.w; near_p_w.z/=near_p_w.w;}
    if(far_p_w.w!=0){far_p_w.x/=far_p_w.w; far_p_w.y/=far_p_w.w; far_p_w.z/=far_p_w.w;}
    vec3_t ro={near_p_w.x,near_p_w.y,near_p_w.z}, rd=vec3_normalize(vec3_sub((vec3_t){far_p_w.x,far_p_w.y,far_p_w.z},ro));
    
    int closest_idx=-1; float closest_dist=FLT_MAX;
    
    for (int i=0; i<g_scene.object_count; i++){
        scene_object_t* obj=g_scene.objects[i];
        mat4_t model_matrix = mat4_get_world_transform(&g_scene, i);

        if (obj->mesh && obj->mesh->face_count > 0) {
            for(int j=0;j<obj->mesh->face_count;j++){
                int v0i=obj->mesh->faces[j*3+0], v1i=obj->mesh->faces[j*3+1], v2i=obj->mesh->faces[j*3+2];
                vec4_t v0w=mat4_mul_vec4(model_matrix,(vec4_t){obj->mesh->vertices[v0i].x,obj->mesh->vertices[v0i].y,obj->mesh->vertices[v0i].z,1});
                vec4_t v1w=mat4_mul_vec4(model_matrix,(vec4_t){obj->mesh->vertices[v1i].x,obj->mesh->vertices[v1i].y,obj->mesh->vertices[v1i].z,1});
                vec4_t v2w=mat4_mul_vec4(model_matrix,(vec4_t){obj->mesh->vertices[v2i].x,obj->mesh->vertices[v2i].y,obj->mesh->vertices[v2i].z,1});
                vec3_t v0={v0w.x,v0w.y,v0w.z}, v1={v1w.x,v1w.y,v1w.z}, v2={v2w.x,v2w.y,v2w.z};
                float dist; 
                if(ray_intersects_triangle(ro,rd,v0,v1,v2,&dist)){
                    if(dist<closest_dist){
                        closest_dist=dist; 
                        closest_idx=i;
                    }
                }
            }
        } else if (obj->light_properties) {
            vec3_t light_pos = { model_matrix.m[0][3], model_matrix.m[1][3], model_matrix.m[2][3] };
            const float click_radius_sq = 0.2f * 0.2f;

            vec3_t oc = vec3_sub(ro, light_pos);
            float b = vec3_dot(oc, rd);
            float c = vec3_dot(oc, oc) - click_radius_sq;
            float discriminant = b*b - c;

            if (discriminant > 0) {
                float dist = -b - sqrtf(discriminant);
                if (dist > 0 && dist < closest_dist) {
                    closest_dist = dist;
                    closest_idx = i;
                }
            }
        }
    } 
    return closest_idx;
}
int find_clicked_vertex(scene_object_t* object, int object_index, int mx, int my) {
    if (!object) return -1;
    mat4_t model_matrix = mat4_get_world_transform(&g_scene, object_index);
    
    // MODIFIED: Scale mouse coordinates from window space to render space
    float scaled_mx = (float)mx * ((float)g_render_width / (float)g_window_width);
    float scaled_my = (float)my * ((float)g_render_height / (float)g_window_height);

    vec3_t offset;
    offset.x = g_camera_distance * cosf(g_camera_pitch) * cosf(g_camera_yaw);
    offset.y = g_camera_distance * cosf(g_camera_pitch) * sinf(g_camera_yaw);
    offset.z = g_camera_distance * sinf(g_camera_pitch);
    vec3_t cam_pos = vec3_add(g_camera_target, offset);

    vec3_t up_vector = {0, 0, 1};
    if (fabs(sinf(g_camera_pitch)) > 0.999f) {
        up_vector = (vec3_t){0, 1, 0};
    }
    mat4_t view_matrix = mat4_look_at(cam_pos, g_camera_target, up_vector);

    // MODIFIED: Use render resolution for aspect ratio
    mat4_t projection_matrix = mat4_perspective(3.14159f/4.0f, (float)g_render_width/(float)g_render_height, 0.1f, 100.0f);
    mat4_t final_transform = mat4_mul_mat4(projection_matrix, mat4_mul_mat4(view_matrix, model_matrix));
    
    int closest_vertex_idx = -1; 
    float closest_dist_sq = FLT_MAX;
    
    for (int i = 0; i < object->mesh->vertex_count; i++) {
        vec4_t v_proj = mat4_mul_vec4(final_transform, (vec4_t){object->mesh->vertices[i].x, object->mesh->vertices[i].y, object->mesh->vertices[i].z, 1.0f});
        if (v_proj.w > 0) {
            float sx = (v_proj.x / v_proj.w + 1.0f) * 0.5f * g_render_width;
            float sy = (1.0f - v_proj.y / v_proj.w) * 0.5f * g_render_height;
            float dx = scaled_mx - sx, dy = scaled_my - sy; 
            float dist_sq = dx*dx + dy*dy;
            if (dist_sq < (8.0f*8.0f) && dist_sq < closest_dist_sq) { 
                closest_dist_sq=dist_sq; 
                closest_vertex_idx=i; 
            }
        }
    } 
    return closest_vertex_idx;
}
int find_clicked_face(scene_object_t* object, int object_index, int mx, int my) {
    if (!object) return -1;

    // MODIFIED: Scale mouse from window space to render space
    float scaled_mx = (float)mx * ((float)g_render_width / (float)g_window_width);
    float scaled_my = (float)my * ((float)g_render_height / (float)g_window_height);

    float xn = (2.0f*scaled_mx)/g_render_width-1.0f, yn=1.0f-(2.0f*scaled_my)/g_render_height;
    mat4_t proj = mat4_perspective(3.14159f/4.0f, (float)g_render_width/(float)g_render_height, 0.1f, 100.0f);
    
    vec3_t offset;
    offset.x = g_camera_distance * cosf(g_camera_pitch) * cosf(g_camera_yaw);
    offset.y = g_camera_distance * cosf(g_camera_pitch) * sinf(g_camera_yaw);
    offset.z = g_camera_distance * sinf(g_camera_pitch);
    vec3_t cam_pos = vec3_add(g_camera_target, offset);
    
    vec3_t up_vector = {0, 0, 1};
    if (fabs(sinf(g_camera_pitch)) > 0.999f) {
        up_vector = (vec3_t){0, 1, 0};
    }
    mat4_t view = mat4_look_at(cam_pos, g_camera_target, up_vector);

    mat4_t inv_vp = mat4_inverse(mat4_mul_mat4(proj, view));
    vec4_t near_p_w = mat4_mul_vec4(inv_vp,(vec4_t){xn,yn,-1,1}), far_p_w=mat4_mul_vec4(inv_vp,(vec4_t){xn,yn,1,1});
    if(near_p_w.w!=0){near_p_w.x/=near_p_w.w; near_p_w.y/=near_p_w.w; near_p_w.z/=near_p_w.w;}
    if(far_p_w.w!=0){far_p_w.x/=far_p_w.w; far_p_w.y/=far_p_w.w; far_p_w.z/=far_p_w.w;}
    vec3_t ro={near_p_w.x,near_p_w.y,near_p_w.z}, rd=vec3_normalize(vec3_sub((vec3_t){far_p_w.x,far_p_w.y,far_p_w.z},ro));
    
    mat4_t model_matrix = mat4_get_world_transform(&g_scene, object_index);
    int closest_face_idx = -1; 
    float closest_dist = FLT_MAX;
    
    for (int j = 0; j < object->mesh->face_count; j++) {
        int v0i=object->mesh->faces[j*3+0], v1i=object->mesh->faces[j*3+1], v2i=object->mesh->faces[j*3+2];
        vec4_t v0w=mat4_mul_vec4(model_matrix,(vec4_t){object->mesh->vertices[v0i].x,object->mesh->vertices[v0i].y,object->mesh->vertices[v0i].z,1});
        vec4_t v1w=mat4_mul_vec4(model_matrix,(vec4_t){object->mesh->vertices[v1i].x,object->mesh->vertices[v1i].y,object->mesh->vertices[v1i].z,1});
        vec4_t v2w=mat4_mul_vec4(model_matrix,(vec4_t){object->mesh->vertices[v2i].x,object->mesh->vertices[v2i].y,object->mesh->vertices[v2i].z,1});
        vec3_t v0={v0w.x,v0w.y,v0w.z}, v1={v1w.x,v1w.y,v1w.z}, v2={v2w.x,v2w.y,v2w.z};
        float dist; 
        if(ray_intersects_triangle(ro,rd,v0,v1,v2,&dist)){
            if(dist<closest_dist){
                closest_dist=dist; 
                closest_face_idx=j;
            }
        }
    } 
    return closest_face_idx;
}
int find_clicked_edge(scene_object_t* object, int object_index, int mx, int my) {
    if (!object || !object->mesh) return -1;
    mat4_t model_matrix = mat4_get_world_transform(&g_scene, object_index);
    
    // MODIFIED: Scale mouse coordinates from window space to render space
    float scaled_mx = (float)mx * ((float)g_render_width / (float)g_window_width);
    float scaled_my = (float)my * ((float)g_render_height / (float)g_window_height);

    vec3_t offset;
    offset.x = g_camera_distance * cosf(g_camera_pitch) * cosf(g_camera_yaw);
    offset.y = g_camera_distance * cosf(g_camera_pitch) * sinf(g_camera_yaw);
    offset.z = g_camera_distance * sinf(g_camera_pitch);
    vec3_t cam_pos = vec3_add(g_camera_target, offset);
    
    vec3_t up_vector = {0, 0, 1};
    if (fabs(sinf(g_camera_pitch)) > 0.999f) {
        up_vector = (vec3_t){0, 1, 0};
    }
    mat4_t view_matrix = mat4_look_at(cam_pos, g_camera_target, up_vector);

    // MODIFIED: Use render resolution for aspect ratio
    mat4_t projection_matrix = mat4_perspective(3.14159f/4.0f,(float)g_render_width/(float)g_render_height,0.1f,100.0f);
    mat4_t final_transform = mat4_mul_mat4(projection_matrix, mat4_mul_mat4(view_matrix, model_matrix));
    
    int best_edge_packed = -1; 
    float closest_dist = FLT_MAX; 
    const float click_threshold = 10.0f;
    
    #define MAX_VERTS_FOR_EDGE_PICK 1024
    if (object->mesh->vertex_count > MAX_VERTS_FOR_EDGE_PICK) return -1;
    char processed_edges[MAX_VERTS_FOR_EDGE_PICK][MAX_VERTS_FOR_EDGE_PICK] = {0};

    vec4_t* screen_verts = (vec4_t*)malloc(object->mesh->vertex_count * sizeof(vec4_t)); 
    if (!screen_verts) return -1;
    
    for(int i=0; i < object->mesh->vertex_count; i++) {
        vec4_t v_proj=mat4_mul_vec4(final_transform,(vec4_t){object->mesh->vertices[i].x,object->mesh->vertices[i].y,object->mesh->vertices[i].z,1.0f}); 
        if(v_proj.w>0){
            screen_verts[i].x=(v_proj.x/v_proj.w+1.0f)*0.5f*g_render_width; 
            screen_verts[i].y=(1.0f-v_proj.y/v_proj.w)*0.5f*g_render_height; 
            screen_verts[i].w=1;
        } else {
            screen_verts[i].w=0;
        }
    }
    
    for(int i=0; i < object->mesh->face_count; i++) {
        int v_indices[3]={object->mesh->faces[i*3+0],object->mesh->faces[i*3+1],object->mesh->faces[i*3+2]}; 
        for(int j=0;j<3;j++){
            int v1_idx=v_indices[j], v2_idx=v_indices[(j+1)%3]; 
            int min_idx=(v1_idx<v2_idx)?v1_idx:v2_idx, max_idx=(v1_idx>v2_idx)?v1_idx:v2_idx; 
            if(processed_edges[min_idx][max_idx]) continue; 
            processed_edges[min_idx][max_idx]=1; 
            if(screen_verts[v1_idx].w==0||screen_verts[v2_idx].w==0) continue; 
            float p1x=screen_verts[v1_idx].x,p1y=screen_verts[v1_idx].y,p2x=screen_verts[v2_idx].x,p2y=screen_verts[v2_idx].y; 
            float dx=p2x-p1x,dy=p2y-p1y,dist; 
            if(dx==0&&dy==0){
                dist=sqrtf(powf(scaled_mx-p1x,2)+powf(scaled_my-p1y,2));
            } else {
                float t=((scaled_mx-p1x)*dx+(scaled_my-p1y)*dy)/(dx*dx+dy*dy); 
                if(t<0) {
                    dist=sqrtf(powf(scaled_mx-p1x,2)+powf(scaled_my-p1y,2));
                } else if(t>1) {
                    dist=sqrtf(powf(scaled_mx-p2x,2)+powf(scaled_my-p2y,2));
                } else {
                    dist=sqrtf(powf(scaled_mx-(p1x+t*dx),2)+powf(scaled_my-(p1y+t*dy),2));
                }
            } 
            if(dist<closest_dist){
                closest_dist=dist; 
                best_edge_packed=(min_idx<<16)|max_idx;
            }
        }
    }
    
    free(screen_verts); 
    return (closest_dist < click_threshold) ? best_edge_packed : -1;
}
vec3_t get_world_pos_on_plane(int mouse_x, int mouse_y) {
    // MODIFIED: Scale mouse from window space to render space
    float scaled_mx = (float)mouse_x * ((float)g_render_width / (float)g_window_width);
    float scaled_my = (float)mouse_y * ((float)g_render_height / (float)g_window_height);

    float xn = (2.0f * scaled_mx) / g_render_width - 1.0f;
    float yn = 1.0f - (2.0f * scaled_my) / g_render_height;

    mat4_t proj;
    // MODIFIED: Use render aspect ratio
    float aspect_ratio = (float)g_render_width / (float)g_render_height;
    if (g_is_orthographic) {
        float ortho_height = g_camera_distance;
        float ortho_width = ortho_height * aspect_ratio;
        proj = mat4_orthographic(-ortho_width / 2.0f, ortho_width / 2.0f, -ortho_height / 2.0f, ortho_height / 2.0f, 0.1f, 100.0f);
    } else {
        proj = mat4_perspective(3.14159f / 4.0f, aspect_ratio, 0.1f, 100.0f);
    }

    vec3_t offset;
    offset.x = g_camera_distance * cosf(g_camera_pitch) * cosf(g_camera_yaw);
    offset.y = g_camera_distance * cosf(g_camera_pitch) * sinf(g_camera_yaw);
    offset.z = g_camera_distance * sinf(g_camera_pitch);
    vec3_t cam_pos = vec3_add(g_camera_target, offset);
    
    vec3_t up_vector = {0, 0, 1};
    if (fabs(sinf(g_camera_pitch)) > 0.999f) {
        up_vector = (vec3_t){0, 1, 0};
    }
    mat4_t view = mat4_look_at(cam_pos, g_camera_target, up_vector);

    mat4_t inv_vp = mat4_inverse(mat4_mul_mat4(proj, view));

    vec4_t near_p_w = mat4_mul_vec4(inv_vp, (vec4_t){xn, yn, -1, 1});
    vec4_t far_p_w = mat4_mul_vec4(inv_vp, (vec4_t){xn, yn, 1, 1});
    if (near_p_w.w != 0) { near_p_w.x /= near_p_w.w; near_p_w.y /= near_p_w.w; near_p_w.z /= near_p_w.w; }
    if (far_p_w.w != 0) { far_p_w.x /= far_p_w.w; far_p_w.y /= far_p_w.w; far_p_w.z /= far_p_w.w; }

    vec3_t ro = {near_p_w.x, near_p_w.y, near_p_w.z};
    vec3_t rd = vec3_normalize(vec3_sub((vec3_t){far_p_w.x, far_p_w.y, far_p_w.z}, ro));

    vec3_t plane_normal = {0, 0, 1};
    float denom = vec3_dot(rd, plane_normal);

    if (fabs(denom) > 1e-6) {
        vec3_t plane_origin = {0, 0, g_draw_plane_height};
        vec3_t p0_l0 = vec3_sub(plane_origin, ro);
        float t = vec3_dot(p0_l0, plane_normal) / denom;
        if (t >= 0) {
            return vec3_add(ro, vec3_scale(rd, t));
        }
    }

    return (vec3_t){g_camera_target.x, g_camera_target.y, g_draw_plane_height};
}
mat4_t mat4_orthographic(float left, float right, float bottom, float top, float near_plane, float far_plane) {
    mat4_t m = {0};
    
    m.m[0][0] = 2.0f / (right - left);
    m.m[1][1] = 2.0f / (top - bottom);
    m.m[2][2] = -2.0f / (far_plane - near_plane);
    
    m.m[0][3] = -(right + left) / (right - left);
    m.m[1][3] = -(top + bottom) / (top - bottom);
    m.m[2][3] = -(far_plane + near_plane) / (far_plane - near_plane);
    m.m[3][3] = 1.0f;
    
    return m;
}
// --- Drawing Helper Functions ---
void draw_pixel(int x,int y,float z,uint32_t c){if(x>=0&&x<g_render_width&&y>=0&&y<g_render_height){int i=x+y*g_render_width;if(z<g_depth_buffer[i]){*((uint32_t*)g_framebuffer_memory+i)=c;g_depth_buffer[i]=z;}}}
void draw_vertex_marker(int x, int y, float z, uint32_t c){
    for(int i=-1; i<=1; i++){
        for(int j=-1; j<=1; j++){
            draw_pixel(x + i, y + j, z - 0.002f, c);
        }
    }
}
void draw_line(int x0,int y0,float z0,int x1,int y1,float z1,uint32_t c){int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy,e2;float z=z0,dz=(z1-z0)/sqrtf((float)(x1-x0)*(x1-x0)+(y1-y0)*(y1-y0));for(;;){draw_pixel(x0,y0,z,c);if(x0==x1&&y0==y1)break;e2=2*err;if(e2>=dy){err+=dy;x0+=sx;z+=dz*sx;}if(e2<=dx){err+=dx;y0+=sy;z+=dz*sy;}}}
void draw_filled_triangle(vec4_t p0, vec4_t p1, vec4_t p2, uint32_t c) {
    if (p0.w <= 0 || p1.w <= 0 || p2.w <= 0) return;

    // Perspective divide
    float p0w_inv = 1.0f / p0.w; p0.x *= p0w_inv; p0.y *= p0w_inv; p0.z *= p0w_inv;
    float p1w_inv = 1.0f / p1.w; p1.x *= p1w_inv; p1.y *= p1w_inv; p1.z *= p1w_inv;
    float p2w_inv = 1.0f / p2.w; p2.x *= p2w_inv; p2.y *= p2w_inv; p2.z *= p2w_inv;

    // Screen space transform
    p0.x = (p0.x + 1.0f) * 0.5f * g_render_width; p0.y = (1.0f - p0.y) * 0.5f * g_render_height;
    p1.x = (p1.x + 1.0f) * 0.5f * g_render_width; p1.y = (1.0f - p1.y) * 0.5f * g_render_height;
    p2.x = (p2.x + 1.0f) * 0.5f * g_render_width; p2.y = (1.0f - p2.y) * 0.5f * g_render_height; // <-- THIS LINE IS FIXED

    // Sort vertices by Y
    if (p0.y > p1.y) { vec4_t t = p0; p0 = p1; p1 = t; float tw = p0w_inv; p0w_inv = p1w_inv; p1w_inv = tw; }
    if (p0.y > p2.y) { vec4_t t = p0; p0 = p2; p2 = t; float tw = p0w_inv; p0w_inv = p2w_inv; p2w_inv = tw; }
    if (p1.y > p2.y) { vec4_t t = p1; p1 = p2; p2 = t; float tw = p1w_inv; p1w_inv = p2w_inv; p2w_inv = tw; }

    int y_start = (int)(p0.y + 0.5f);
    int y_end = (int)(p2.y + 0.5f);

    y_start = (y_start < 0) ? 0 : y_start;
    y_end = (y_end > g_render_height) ? g_render_height : y_end;
    
    float dy_total = p2.y - p0.y;
    float dy_split = p1.y - p0.y;

    for (int y = y_start; y < y_end; y++) {
        float factor1 = (dy_total > 0) ? ((float)y - p0.y) / dy_total : 0;
        float factor2 = (y < p1.y) 
            ? ((dy_split > 0) ? ((float)y - p0.y) / dy_split : 0)
            : ((p2.y - p1.y > 0) ? ((float)y - p1.y) / (p2.y - p1.y) : 0);

        float xa_f = p0.x + factor1 * (p2.x - p0.x);
        float wa_inv = p0w_inv + factor1 * (p2w_inv - p0w_inv);

        float xb_f = (y < p1.y) 
            ? (p0.x + factor2 * (p1.x - p0.x)) 
            : (p1.x + factor2 * (p2.x - p1.x));
        float wb_inv = (y < p1.y)
            ? (p0w_inv + factor2 * (p1w_inv - p0w_inv))
            : (p1w_inv + factor2 * (p2w_inv - p1w_inv));

        if (xa_f > xb_f) { 
            float temp_x = xa_f; xa_f = xb_f; xb_f = temp_x;
            float temp_w = wa_inv; wa_inv = wb_inv; wb_inv = temp_w;
        }

        int x_start = (int)(xa_f + 0.5f);
        int x_end = (int)(xb_f + 0.5f);

        x_start = (x_start < 0) ? 0 : x_start;
        x_end = (x_end > g_render_width) ? g_render_width : x_end;
        
        float scanline_width = xb_f - xa_f;
        if (scanline_width <= 0) continue;

        float w_inv_step = (wb_inv - wa_inv) / scanline_width;
        float initial_offset = (float)x_start - xa_f;
        float current_w_inv = wa_inv + w_inv_step * initial_offset;
        
        uint32_t* row = (uint32_t*)g_framebuffer_memory + y * g_render_width;
        float* depth_row = g_depth_buffer + y * g_render_width;

        for (int x = x_start; x < x_end; x++) {
            if (current_w_inv > 0) {
                float z = 1.0f / current_w_inv;
                if (z < depth_row[x]) {
                    row[x] = c;
                    depth_row[x] = z;
                }
            }
            current_w_inv += w_inv_step;
        }
    }
}
void draw_gouraud_triangle(vec4_t p0, vec4_t p1, vec4_t p2, vec3_t c0, vec3_t c1, vec3_t c2) {
    if (p0.w <= 0 || p1.w <= 0 || p2.w <= 0) return;

    // Perspective divide
    float p0w_inv = 1.0f / p0.w; p0.x *= p0w_inv; p0.y *= p0w_inv; p0.z *= p0w_inv;
    float p1w_inv = 1.0f / p1.w; p1.x *= p1w_inv; p1.y *= p1w_inv; p1.z *= p1w_inv;
    float p2w_inv = 1.0f / p2.w; p2.x *= p2w_inv; p2.y *= p2w_inv; p2.z *= p2w_inv;

    // Screen space transform
    p0.x = (p0.x + 1.0f) * 0.5f * g_render_width;  p0.y = (1.0f - p0.y) * 0.5f * g_render_height;
    p1.x = (p1.x + 1.0f) * 0.5f * g_render_width;  p1.y = (1.0f - p1.y) * 0.5f * g_render_height;
    p2.x = (p2.x + 1.0f) * 0.5f * g_render_width;  p2.y = (1.0f - p2.y) * 0.5f * g_render_height;

    // Pre-calculate colors divided by w for perspective-correct interpolation
    vec3_t c0_pw = vec3_scale(c0, p0w_inv);
    vec3_t c1_pw = vec3_scale(c1, p1w_inv);
    vec3_t c2_pw = vec3_scale(c2, p2w_inv);

    // Sort vertices by Y
    if (p0.y > p1.y) { vec4_t tp = p0; p0 = p1; p1 = tp; vec3_t tc = c0_pw; c0_pw = c1_pw; c1_pw = tc; float tw = p0w_inv; p0w_inv = p1w_inv; p1w_inv = tw; }
    if (p0.y > p2.y) { vec4_t tp = p0; p0 = p2; p2 = tp; vec3_t tc = c0_pw; c0_pw = c2_pw; c2_pw = tc; float tw = p0w_inv; p0w_inv = p2w_inv; p2w_inv = tw; }
    if (p1.y > p2.y) { vec4_t tp = p1; p1 = p2; p2 = tp; vec3_t tc = c1_pw; c1_pw = c2_pw; c2_pw = tc; float tw = p1w_inv; p1w_inv = p2w_inv; p2w_inv = tw; }

    int y_start = (int)(p0.y + 0.5f);
    int y_end = (int)(p2.y + 0.5f);

    y_start = (y_start < 0) ? 0 : y_start;
    y_end = (y_end > g_render_height) ? g_render_height : y_end;

    float dy_total = p2.y - p0.y;
    float dy_split = p1.y - p0.y;

    for (int y = y_start; y < y_end; y++) {
        float factor1 = (dy_total > 0) ? ((float)y - p0.y) / dy_total : 0;
        float factor2 = (y < p1.y) 
            ? ((dy_split > 0) ? ((float)y - p0.y) / dy_split : 0)
            : ((p2.y - p1.y > 0) ? ((float)y - p1.y) / (p2.y - p1.y) : 0);

        float xa_f = p0.x + factor1 * (p2.x - p0.x);
        float wa_inv = p0w_inv + factor1 * (p2w_inv - p0w_inv);
        vec3_t ca_pw = vec3_add(c0_pw, vec3_scale(vec3_sub(c2_pw, c0_pw), factor1));

        float xb_f = (y < p1.y) ? (p0.x + factor2 * (p1.x - p0.x)) : (p1.x + factor2 * (p2.x - p1.x));
        float wb_inv = (y < p1.y) ? (p0w_inv + factor2 * (p1w_inv - p0w_inv)) : (p1w_inv + factor2 * (p2w_inv - p1w_inv));
        vec3_t cb_pw = (y < p1.y) ? vec3_add(c0_pw, vec3_scale(vec3_sub(c1_pw, c0_pw), factor2)) : vec3_add(c1_pw, vec3_scale(vec3_sub(c2_pw, c1_pw), factor2));

        if (xa_f > xb_f) { 
            float temp_x = xa_f; xa_f = xb_f; xb_f = temp_x;
            float temp_w = wa_inv; wa_inv = wb_inv; wb_inv = temp_w;
            vec3_t temp_c = ca_pw; ca_pw = cb_pw; cb_pw = temp_c;
        }

        int x_start = (int)(xa_f + 0.5f);
        int x_end = (int)(xb_f + 0.5f);

        x_start = (x_start < 0) ? 0 : x_start;
        x_end = (x_end > g_render_width) ? g_render_width : x_end;

        float scanline_width = xb_f - xa_f;
        if (scanline_width <= 0) continue;

        vec3_t c_pw_step = vec3_scale(vec3_sub(cb_pw, ca_pw), 1.0f / scanline_width);
        float w_inv_step = (wb_inv - wa_inv) / scanline_width;

        float initial_offset = (float)x_start - xa_f;
        vec3_t current_c_pw = vec3_add(ca_pw, vec3_scale(c_pw_step, initial_offset));
        float current_w_inv = wa_inv + w_inv_step * initial_offset;
        
        uint32_t* row = (uint32_t*)g_framebuffer_memory + y * g_render_width;
        float* depth_row = g_depth_buffer + y * g_render_width;

        for (int x = x_start; x < x_end; x++) {
            if (current_w_inv > 0) {
                float z = 1.0f / current_w_inv;
                if (z < depth_row[x]) {
                    vec3_t final_color = vec3_scale(current_c_pw, z);

                    uint8_t r = (uint8_t)(fmin(1.0f, final_color.x) * 255.0f);
                    uint8_t g = (uint8_t)(fmin(1.0f, final_color.y) * 255.0f);
                    uint8_t b = (uint8_t)(fmin(1.0f, final_color.z) * 255.0f);
                    
                    row[x] = (r << 16) | (g << 8) | b;
                    depth_row[x] = z;
                }
            }
            current_c_pw = vec3_add(current_c_pw, c_pw_step);
            current_w_inv += w_inv_step;
        }
    }
}
void draw_pixel_thick(int x, int y, float z, uint32_t color) {
    for (int i = -1; i <= 1; i++) {
        for (int j = -1; j <= 1; j++) {
            draw_pixel(x + i, y + j, z - 0.002f, color);
        }
    }
}
void draw_thick_line(int x0, int y0, float z0, int x1, int y1, float z1, uint32_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    float z = z0;
    float line_length = sqrtf((float)(x1 - x0) * (x1 - x0) + (float)(y1 - y0) * (y1 - y0));
    float dz = (line_length > 0) ? (z1 - z0) / line_length : 0;

    for (;;) {
        draw_pixel_thick(x0, y0, z, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
            z += dz;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
            z += dz;
        }
    }
}
LRESULT CALLBACK window_callback(HWND window_handle, UINT message, WPARAM w_param, LPARAM l_param) {
    LRESULT result = 0;
    switch (message) {
case WM_CLOSE: case WM_DESTROY: {
            scene_destroy(&g_scene);
            selection_destroy(&g_selected_objects);
            selection_destroy(&g_selected_components);
            destroy_mesh_data(g_cube_mesh_data);
            destroy_mesh_data(g_pyramid_mesh_data);
            destroy_mesh_data(g_sphere_mesh_data);
            destroy_mesh_data(g_vertex_mesh_data);
            destroy_mesh_data(g_edge_mesh_data);
            destroy_mesh_data(g_face_mesh_data);
            destroy_mesh_data(g_player_spawn_mesh_data);
            if(g_transform_initial_vertices) free(g_transform_initial_vertices);
            if(g_clip_coords_buffer) free(g_clip_coords_buffer);
            if(g_colors_buffer) free(g_colors_buffer);
            PostQuitMessage(0);
        } break;
        case WM_SIZE: {
            g_window_width = LOWORD(l_param);
            g_window_height = HIWORD(l_param);
            
            if (g_framebuffer_bitmap) DeleteObject(g_framebuffer_bitmap);
            if (g_depth_buffer) free(g_depth_buffer);

            // --- MODIFIED: Create framebuffer based on fixed render resolution ---
            g_framebuffer_info.bmiHeader.biSize = sizeof(g_framebuffer_info.bmiHeader);
            g_framebuffer_info.bmiHeader.biWidth = g_render_width;
            g_framebuffer_info.bmiHeader.biHeight = -g_render_height; // Top-down
            g_framebuffer_info.bmiHeader.biPlanes = 1;
            g_framebuffer_info.bmiHeader.biBitCount = 32;
            g_framebuffer_info.bmiHeader.biCompression = BI_RGB;

            HDC hdc = GetDC(window_handle);
            g_framebuffer_bitmap = CreateDIBSection(
                hdc, &g_framebuffer_info, DIB_RGB_COLORS,
                &g_framebuffer_memory,
                NULL, 0
            );
            ReleaseDC(window_handle, hdc);
            
            g_depth_buffer = (float*)malloc(g_render_width * g_render_height * sizeof(float));
            
        } break;
        case WM_MBUTTONDOWN: {
            g_middle_mouse_down = 1; g_mouse_dragged = 0;
            g_last_mouse_x = LOWORD(l_param); g_last_mouse_y = HIWORD(l_param);
            SetCapture(window_handle);
        } break;
        case WM_MBUTTONUP: {
            g_middle_mouse_down = 0; ReleaseCapture();
        } break;
case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(w_param);

            if (g_current_tool == TOOL_DRAW_FACE && (GetKeyState(VK_CONTROL) & 0x8000)) {
                if (delta > 0) {
                    g_draw_plane_height += 1.0f;
                } else {
                    g_draw_plane_height -= 1.0f;
                }
            }

            else if (GetKeyState(VK_CONTROL) & 0x8000 && g_selected_objects.count == 1) {
                float amt=0.1f;
                scene_object_t* obj=g_scene.objects[g_selected_objects.items[0]];
                if (delta>0){obj->scale.x+=amt;obj->scale.y+=amt;obj->scale.z+=amt;}
                else{obj->scale.x-=amt;obj->scale.y-=amt;obj->scale.z-=amt;}
                if(obj->scale.x<0.1f)obj->scale.x=0.1f; if(obj->scale.y<0.1f)obj->scale.y=0.1f; if(obj->scale.z<0.1f)obj->scale.z=0.1f;
            } else {
                if (delta>0)g_camera_distance-=0.5f; else g_camera_distance+=0.5f;
                if(g_camera_distance<2.0f)g_camera_distance=2.0f; if(g_camera_distance>40.0f)g_camera_distance=40.0f;
            }
        } break;
case WM_LBUTTONDOWN: {
            int mouse_x = LOWORD(l_param);
            int mouse_y = HIWORD(l_param);
            
            // --- NEW: Scale mouse coordinates to render space for UI interaction ---
            float scaled_mx = (float)mouse_x * ((float)g_render_width / (float)g_window_width);
            float scaled_my = (float)mouse_y * ((float)g_render_height / (float)g_window_height);
            POINT pt = {(int)scaled_mx, (int)scaled_my};

            if (g_hEdit) {
                // Edit box logic still uses screen coordinates, so this is an exception
                RECT edit_rect;
                GetWindowRect(g_hEdit, &edit_rect);
                POINT screen_pt = {mouse_x, mouse_y};
                ClientToScreen(window_handle, &screen_pt);
                if (!PtInRect(&edit_rect, screen_pt)) {
                    destroy_and_apply_coord_edit();
                } else {
                    return 0;
                }
            }

            // --- MODIFIED: Use scaled point for all PtInRect checks ---
            for (int i = 0; i < 2; i++) {
                if (PtInRect(&g_mode_rects[i], pt)) {
                    editor_mode_t new_mode = (editor_mode_t)i;
                    if (g_current_editor_mode != new_mode) {
                         g_current_editor_mode = new_mode;
                         selection_clear(&g_selected_objects);
                         selection_clear(&g_selected_components);
                         g_current_mode = MODE_OBJECT;
                         if (g_current_editor_mode == EDITOR_MODEL) {
                             scene_clear(&g_scene);
                             scene_add_object(&g_scene, g_cube_mesh_data, (vec3_t){0, 0, 0});
                             g_camera_target = (vec3_t){0, 0, 0};
                         }
                    }
                    return 0;
                }
            }

            for (int i = 0; i < 3; i++) {
                if (PtInRect(&g_shading_rects[i], pt)) {
                    g_shading_mode = (shading_mode_t)i;
                    return 0;
                }
            }
            
            for (int i = 0; i < 2; i++) {
                if (PtInRect(&g_tool_rects[i], pt)) {
                    g_current_tool = (tool_mode_t)i;
                    return 0;
                }
            }

            if (g_selected_objects.count > 0 && PtInRect(&g_color_swatch_rect, pt)) {
                CHOOSECOLOR cc;
                static COLORREF acrCustClr[16]; 
                ZeroMemory(&cc, sizeof(cc));
                cc.lStructSize = sizeof(cc);
                cc.hwndOwner = window_handle;
                cc.lpCustColors = (LPDWORD) acrCustClr;
                scene_object_t* first_object = g_scene.objects[g_selected_objects.items[0]];
                vec3_t initial_color;
                if (first_object->light_properties) {
                    initial_color = first_object->light_properties->color;
                } else {
                    initial_color = first_object->material.diffuse_color;
                }
                cc.rgbResult = RGB((BYTE)(initial_color.x * 255), (BYTE)(initial_color.y * 255), (BYTE)(initial_color.z * 255));
                cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                if (ChooseColor(&cc) == TRUE) {
                    vec3_t new_color = { GetRValue(cc.rgbResult) / 255.0f, GetGValue(cc.rgbResult) / 255.0f, GetBValue(cc.rgbResult) / 255.0f };
                    for (int i = 0; i < g_selected_objects.count; i++) {
                        scene_object_t* obj_to_color = g_scene.objects[g_selected_objects.items[i]];
                        if (obj_to_color->light_properties) {
                            obj_to_color->light_properties->color = new_color;
                        } else {
                            obj_to_color->material.diffuse_color = new_color;
                        }
                    }
                }
                return 0;
            }

            if (PtInRect(&g_sky_color_swatch_rect, pt)) {
                CHOOSECOLOR cc;
                static COLORREF acrCustClr[16]; 
                ZeroMemory(&cc, sizeof(cc));
                cc.lStructSize = sizeof(cc);
                cc.hwndOwner = window_handle;
                cc.lpCustColors = (LPDWORD) acrCustClr;
                cc.rgbResult = RGB((BYTE)(g_sky_color.x * 255), (BYTE)(g_sky_color.y * 255), (BYTE)(g_sky_color.z * 255));
                cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                if (ChooseColor(&cc) == TRUE) {
                    g_sky_color.x = GetRValue(cc.rgbResult) / 255.0f;
                    g_sky_color.y = GetGValue(cc.rgbResult) / 255.0f;
                    g_sky_color.z = GetBValue(cc.rgbResult) / 255.0f;
                }
                return 0;
            }
            
            // Coordinate/Material text editing is omitted for brevity but should also use scaled 'pt'
            
            if (g_current_tool == TOOL_SELECT) {
                g_mouse_down = 1;
                g_mouse_dragged = 0;
                g_last_mouse_x = mouse_x;
                g_last_mouse_y = mouse_y;
                SetCapture(window_handle);
                int outliner_panel_x = g_render_width - 210;
                if (scaled_mx >= outliner_panel_x) { // Check scaled coordinate
                    int is_shift_down = GetKeyState(VK_SHIFT) & 0x8000;
                    int clicked_outliner_object = -1;
                    for (int i = 0; i < g_scene.object_count; i++) { if (PtInRect(&g_scene.objects[i]->ui_outliner_rect, pt)) { clicked_outliner_object = i; break; } }
                    if (clicked_outliner_object != -1) {
                        int was_already_selected = selection_contains(&g_selected_objects, clicked_outliner_object);
                        if (is_shift_down) {
                            if (was_already_selected) { selection_remove(&g_selected_objects, clicked_outliner_object); } else { selection_add(&g_selected_objects, clicked_outliner_object); }
                        } else {
                            if (was_already_selected && g_selected_objects.count == 1) { selection_clear(&g_selected_objects); selection_clear(&g_selected_components); } else { selection_clear(&g_selected_objects); selection_clear(&g_selected_components); selection_add(&g_selected_objects, clicked_outliner_object); }
                        }
                    }
                    g_is_box_selecting = 0;
                    return 0;
                }
                // Pass original mouse_x, mouse_y as the picking functions now handle scaling internally
                int clicked_object = find_clicked_object(mouse_x, mouse_y);
                if (clicked_object == -1) {
                    g_is_box_selecting = 1;
                    // MODIFIED: Use scaled coordinates for selection box
                    g_selection_box_rect.left = (int)scaled_mx; g_selection_box_rect.top = (int)scaled_my;
                    g_selection_box_rect.right = (int)scaled_mx; g_selection_box_rect.bottom = (int)scaled_my;
                }
            } else if (g_current_tool == TOOL_DRAW_FACE) {
                g_mouse_down = 1;
                g_mouse_dragged = 0;
                g_last_mouse_x = mouse_x;
                g_last_mouse_y = mouse_y;
                SetCapture(window_handle);
                g_last_tile_pos = (vec3_t){FLT_MAX, FLT_MAX, FLT_MAX};
                vec3_t world_pos = get_world_pos_on_plane(mouse_x, mouse_y);
                world_pos.x = roundf(world_pos.x);
                world_pos.y = roundf(world_pos.y);
                world_pos.z = roundf(world_pos.z);
                create_grid_face_at(world_pos);
                g_last_tile_pos = world_pos;
            }

        } break;
case WM_LBUTTONUP: {
            int mouse_x = LOWORD(l_param);
            int mouse_y = HIWORD(l_param);
            int is_shift_down = GetKeyState(VK_SHIFT) & 0x8000;
            
            // --- NEW: Scale mouse coordinates to render space for UI interaction ---
            float scaled_mx = (float)mouse_x * ((float)g_render_width / (float)g_window_width);
            float scaled_my = (float)mouse_y * ((float)g_render_height / (float)g_window_height);

            if (g_current_transform_mode != TRANSFORM_NONE) {
                if (g_current_mode == MODE_EDIT && g_selected_objects.count > 0) {
                    scene_object_t* obj = g_scene.objects[g_selected_objects.items[0]];
                    if (obj && obj->mesh) {
                        mesh_calculate_normals(obj->mesh);
                    }
                }
                
                g_current_transform_mode = TRANSFORM_NONE;
                g_transform_axis_is_locked = 0;
                if (g_transform_initial_vertices) {
                    free(g_transform_initial_vertices);
                    g_transform_initial_vertices = NULL;
                }
            }
            else if (g_is_box_selecting) {
                g_is_box_selecting = 0;
                // Update selection rect with final scaled coordinates before normalizing
                g_selection_box_rect.right = (int)scaled_mx;
                g_selection_box_rect.bottom = (int)scaled_my;
                normalize_rect(&g_selection_box_rect);

                if (!is_shift_down) {
                    selection_clear(&g_selected_objects);
                    selection_clear(&g_selected_components);
                }
                
                if (g_current_mode == MODE_OBJECT) {
                    for (int i = 0; i < g_scene.object_count; i++) {
                        scene_object_t* obj = g_scene.objects[i];
                        mat4_t world_matrix = mat4_get_world_transform(&g_scene, i);
                        vec3_t p = {world_matrix.m[0][3], world_matrix.m[1][3], world_matrix.m[2][3]};

                        vec3_t offset;
                        offset.x = g_camera_distance * cosf(g_camera_pitch) * cosf(g_camera_yaw);
                        offset.y = g_camera_distance * cosf(g_camera_pitch) * sinf(g_camera_yaw);
                        offset.z = g_camera_distance * sinf(g_camera_pitch);
                        vec3_t cam_pos=vec3_add(g_camera_target, offset);
                        mat4_t view = mat4_look_at(cam_pos, g_camera_target, (vec3_t){0,0,1});
                        mat4_t proj = mat4_perspective(3.14159f/4.0f, (float)g_render_width/(float)g_render_height, 0.1f, 100.0f);
                        mat4_t final = mat4_mul_mat4(proj, view);
                        vec4_t clip = mat4_mul_vec4(final, (vec4_t){p.x, p.y, p.z, 1.0f});
                        if (clip.w > 0) {
                            float sx = (clip.x/clip.w+1)*0.5f*g_render_width;
                            float sy = (1-clip.y/clip.w)*0.5f*g_render_height;
                            POINT pt = {(LONG)sx, (LONG)sy};
                            if (PtInRect(&g_selection_box_rect, pt)) {
                                selection_add(&g_selected_objects, i);
                            }
                        }
                    }
                } else if (g_current_mode == MODE_EDIT && g_selected_objects.count > 0) {
                    int obj_idx = g_selected_objects.items[0];
                    scene_object_t* object = g_scene.objects[obj_idx];
                    mat4_t model_matrix = mat4_get_world_transform(&g_scene, obj_idx);
                    
                    vec3_t offset;
                    offset.x = g_camera_distance * cosf(g_camera_pitch) * cosf(g_camera_yaw);
                    offset.y = g_camera_distance * cosf(g_camera_pitch) * sinf(g_camera_yaw);
                    offset.z = g_camera_distance * sinf(g_camera_pitch);
                    vec3_t cam_pos=vec3_add(g_camera_target, offset);
                    mat4_t view = mat4_look_at(cam_pos, g_camera_target, (vec3_t){0,0,1});
                    mat4_t proj = mat4_perspective(3.14159f/4.0f, (float)g_render_width/(float)g_render_height, 0.1f, 100.0f);
                    mat4_t final = mat4_mul_mat4(proj, mat4_mul_mat4(view, model_matrix));
                    
                    if (g_edit_mode_component == EDIT_VERTICES) {
                        for (int i = 0; i < object->mesh->vertex_count; i++) {
                             vec4_t clip = mat4_mul_vec4(final, (vec4_t){object->mesh->vertices[i].x, object->mesh->vertices[i].y, object->mesh->vertices[i].z, 1.0f});
                             if (clip.w > 0) {
                                float sx = (clip.x/clip.w+1)*0.5f*g_render_width;
                                float sy = (1-clip.y/clip.w)*0.5f*g_render_height;
                                POINT pt = {(LONG)sx, (LONG)sy};
                                if (PtInRect(&g_selection_box_rect, pt)) {
                                    selection_add(&g_selected_components, i);
                                }
                             }
                        }
                    }
                }
            }
            else if (!g_mouse_dragged) {
                if (!is_shift_down) {
                    selection_clear(&g_selected_objects);
                    selection_clear(&g_selected_components);
                }
                
                int clicked_obj_idx = find_clicked_object(mouse_x, mouse_y);
                if (clicked_obj_idx != -1) {
                    selection_add(&g_selected_objects, clicked_obj_idx);
                    
                    if (g_current_mode == MODE_EDIT) {
                        scene_object_t* obj = g_scene.objects[clicked_obj_idx];
                        int clicked_comp_idx = -1;
                        switch(g_edit_mode_component) {
                            case EDIT_FACES:    clicked_comp_idx = find_clicked_face(obj, clicked_obj_idx, mouse_x, mouse_y); break;
                            case EDIT_VERTICES: clicked_comp_idx = find_clicked_vertex(obj, clicked_obj_idx, mouse_x, mouse_y); break;
                            case EDIT_EDGES:    clicked_comp_idx = find_clicked_edge(obj, clicked_obj_idx, mouse_x, mouse_y); break;
                        }
                        if (clicked_comp_idx != -1) {
                           selection_add(&g_selected_components, clicked_comp_idx);
                        }
                    }
                }
            }
            g_mouse_down = 0;
            ReleaseCapture();
        } break;
        case WM_RBUTTONDOWN: {
            if (g_current_transform_mode != TRANSFORM_NONE) {
                if (g_selected_objects.count > 0) {
                    scene_object_t* obj = g_scene.objects[g_selected_objects.items[0]];
                    if (g_current_mode == MODE_OBJECT) {
                        if (obj->light_properties && g_current_transform_mode == TRANSFORM_LIGHT_INTENSITY) {
                            obj->light_properties->intensity = g_transform_initial_intensity;
                        } else {
                            obj->position = g_transform_initial_position;
                            obj->rotation = g_transform_initial_rotation;
                            obj->scale    = g_transform_initial_scale;
                        }
                    } else if (g_current_mode == MODE_EDIT && g_transform_initial_vertices) {
                        memcpy(obj->mesh->vertices, g_transform_initial_vertices, obj->mesh->vertex_count * sizeof(vec3_t));
                        free(g_transform_initial_vertices);
                        g_transform_initial_vertices = NULL;
                    }
                }
                g_current_transform_mode = TRANSFORM_NONE;
                g_transform_axis_is_locked = 0;
            }
        } break;
case WM_RBUTTONUP: {
            GetCursorPos(&g_last_right_click_pos);
            HMENU hMenu = CreatePopupMenu();
            HMENU hAddMenu = CreatePopupMenu();
            HMENU hAddMeshMenu = CreatePopupMenu();
            HMENU hAddLightMenu = CreatePopupMenu();

            if (g_current_editor_mode == EDITOR_SCENE) {
                AppendMenu(hMenu, MF_STRING, ID_LOAD_SCENE, "Load Scene");
                AppendMenu(hMenu, MF_STRING, ID_SAVE_SCENE, "Save Scene");
                AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenu(hMenu, MF_STRING, ID_IMPORT_MODEL, "Import Model...");
                
                int can_export = 0;
                for (int i = 0; i < g_selected_objects.count; i++) {
                    if (g_scene.objects[g_selected_objects.items[i]]->mesh) {
                        can_export = 1;
                        break;
                    }
                }
                if (can_export) {
                    AppendMenu(hMenu, MF_STRING, ID_EXPORT_MODEL, "Export Selection As Model...");
                }

                AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            } else { // EDITOR_MODEL
                AppendMenu(hMenu, MF_STRING, ID_LOAD_MODEL, "Load Model...");
                AppendMenu(hMenu, MF_STRING, ID_SAVE_MODEL, "Save Model As...");
                AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenu(hMenu, MF_STRING, ID_CLEAR_SCENE, "New Model");
                AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            }
            
            AppendMenu(hAddMeshMenu, MF_STRING, ID_ADD_CUBE, "Cube");
            AppendMenu(hAddMeshMenu, MF_STRING, ID_ADD_PYRAMID, "Pyramid");
            AppendMenu(hAddMeshMenu, MF_STRING, ID_ADD_SPHERE, "Sphere");
            AppendMenu(hAddMeshMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hAddMeshMenu, MF_STRING, ID_ADD_FACE, "Face");
            AppendMenu(hAddMeshMenu, MF_STRING, ID_ADD_EDGE, "Edge");
            AppendMenu(hAddMeshMenu, MF_STRING, ID_ADD_VERTEX, "Vertex");

            AppendMenu(hAddLightMenu, MF_STRING, ID_ADD_POINT_LIGHT, "Point");
            AppendMenu(hAddLightMenu, MF_STRING, ID_ADD_SPOT_LIGHT, "Spot");
            
            AppendMenu(hAddMenu, MF_POPUP, (UINT_PTR)hAddMeshMenu, "Mesh");
            AppendMenu(hAddMenu, MF_POPUP, (UINT_PTR)hAddLightMenu, "Light");
            
            if (g_current_editor_mode == EDITOR_SCENE) {
                AppendMenu(hAddMenu, MF_SEPARATOR, 0, NULL);
                AppendMenu(hAddMenu, MF_STRING, ID_ADD_PLAYER_SPAWN, "Player Spawn");
            }

            AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hAddMenu, "Add");

            if (g_selected_objects.count > 0) {
                AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenu(hMenu, MF_STRING, ID_DELETE_OBJECT, "Delete");
                
                if (g_selected_objects.count == 1) {
                    scene_object_t* obj = g_scene.objects[g_selected_objects.items[0]];

                    if (obj->mesh) {
                        AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);

                        int is_player_model_checked = obj->is_player_model;
                        AppendMenu(hMenu, MF_STRING | (is_player_model_checked ? MF_CHECKED : MF_UNCHECKED), ID_SET_PLAYER_MODEL, "Player Model");
                        
                        if (is_player_model_checked) {
                            UINT flags = MF_STRING;
                            if (g_current_mode != MODE_EDIT || g_edit_mode_component != EDIT_VERTICES || g_selected_components.count == 0) {
                                flags |= MF_GRAYED;
                            }
                            AppendMenu(hMenu, flags, ID_SET_CAMERA_TARGET, "Set Camera Target from Selection");
                        }
                        
                        AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);

                        int is_double_sided_checked = obj->is_double_sided;
                        AppendMenu(hMenu, MF_STRING | (is_double_sided_checked ? MF_CHECKED : MF_UNCHECKED), ID_TOGGLE_DOUBLE_SIDED, "Double Sided");

                        int is_collision_checked = obj->has_collision;
                        AppendMenu(hMenu, MF_STRING | (is_collision_checked ? MF_CHECKED : MF_UNCHECKED), ID_TOGGLE_COLLISION, "Collision");
                    }

                    if (g_current_editor_mode == EDITOR_SCENE) {
                        int is_static_checked = obj->is_static;
                        AppendMenu(hMenu, MF_STRING | (is_static_checked ? MF_CHECKED : MF_UNCHECKED), ID_TOGGLE_STATIC, "Static");
                    }
                }
            }
            
            if (g_current_editor_mode == EDITOR_SCENE) {
                if (g_selected_objects.count > 1) {
                    AppendMenu(hMenu, MF_STRING, ID_PARENT_OBJECT, "Parent");
                }
                
                int can_unparent = 0;
                if (g_selected_objects.count > 0) {
                    for (int i = 0; i < g_selected_objects.count; i++) {
                        if (g_scene.objects[g_selected_objects.items[i]]->parent_index != -1) {
                            can_unparent = 1;
                            break;
                        }
                    }
                }
                if (can_unparent) {
                    AppendMenu(hMenu, MF_STRING, ID_UNPARENT_OBJECT, "Unparent");
                }
            }

            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, g_last_right_click_pos.x, g_last_right_click_pos.y, 0, window_handle, NULL);
            DestroyMenu(hMenu);
        } break;
case WM_COMMAND: {
            if (LOWORD(w_param) == ID_EDIT_COORD) {
                if (HIWORD(w_param) == EN_KILLFOCUS || HIWORD(w_param) == 0) {
                    destroy_and_apply_coord_edit();
                    return 0;
                }
            }
            ScreenToClient(window_handle, &g_last_right_click_pos);
            vec3_t new_pos = get_world_pos_on_plane(g_last_right_click_pos.x, g_last_right_click_pos.y);
            switch (LOWORD(w_param)) {
                case ID_LOAD_SCENE: trigger_load_scene_dialog(); break;
                case ID_SAVE_SCENE: trigger_save_scene_dialog(); break;
                case ID_LOAD_MODEL: trigger_load_model_dialog(); break;
                case ID_SAVE_MODEL: trigger_save_model_dialog(); break;
                case ID_IMPORT_MODEL: trigger_load_model_dialog(); break;
                case ID_EXPORT_MODEL: trigger_export_selection_dialog(); break;
                case ID_CLEAR_SCENE:
                    scene_clear(&g_scene);
                    selection_clear(&g_selected_objects);
                    selection_clear(&g_selected_components);
                    if (g_current_editor_mode == EDITOR_MODEL) {
                        scene_add_object(&g_scene, g_cube_mesh_data, (vec3_t){0,0,0});
                    }
                    break;
                case ID_DELETE_OBJECT:
                    if (g_selected_objects.count > 0) {
                        qsort(g_selected_objects.items, g_selected_objects.count, sizeof(int), compare_ints_desc);
                        for(int i=0; i<g_selected_objects.count; ++i) {
                            scene_remove_object(&g_scene, g_selected_objects.items[i]);
                        }
                        selection_clear(&g_selected_objects);
                        selection_clear(&g_selected_components);
                    }
                    break;
                case ID_ADD_CUBE:
                    if (g_current_editor_mode == EDITOR_MODEL && g_scene.object_count > 0) {
                        MessageBox(g_window_handle, "Model Mode only supports one object. Use 'New Model' to start over.", "Action Blocked", MB_OK | MB_ICONINFORMATION);
                        break;
                    }
                    scene_add_object(&g_scene, g_cube_mesh_data, new_pos);
                    break;
                case ID_ADD_PYRAMID:
                    if (g_current_editor_mode == EDITOR_MODEL && g_scene.object_count > 0) {
                        MessageBox(g_window_handle, "Model Mode only supports one object. Use 'New Model' to start over.", "Action Blocked", MB_OK | MB_ICONINFORMATION);
                        break;
                    }
                    scene_add_object(&g_scene, g_pyramid_mesh_data, new_pos);
                    break;
                case ID_ADD_SPHERE:
                    if (g_current_editor_mode == EDITOR_MODEL && g_scene.object_count > 0) {
                        MessageBox(g_window_handle, "Model Mode only supports one object. Use 'New Model' to start over.", "Action Blocked", MB_OK | MB_ICONINFORMATION);
                        break;
                    }
                    scene_add_object(&g_scene, g_sphere_mesh_data, new_pos);
                    break;
                case ID_ADD_PLAYER_SPAWN: {
                    int spawn_exists = 0;
                    for (int i = 0; i < g_scene.object_count; i++) {
                        if (g_scene.objects[i]->is_player_spawn) {
                            spawn_exists = 1;
                            break;
                        }
                    }
                    if (spawn_exists) {
                        MessageBox(g_window_handle, "A Player Spawn object already exists in the scene.", "Action Blocked", MB_OK | MB_ICONINFORMATION);
                    } else {
                        scene_add_object(&g_scene, g_player_spawn_mesh_data, new_pos);
                    }
                    break;
                }
                case ID_ADD_FACE:
                    if (g_current_editor_mode == EDITOR_MODEL && g_scene.object_count > 0) {
                        MessageBox(g_window_handle, "Model Mode only supports one object. Use 'New Model' to start over.", "Action Blocked", MB_OK | MB_ICONINFORMATION);
                        break;
                    }
                    scene_add_object(&g_scene, g_face_mesh_data, new_pos);
                    break;
                case ID_ADD_EDGE:
                    if (g_current_editor_mode == EDITOR_MODEL && g_scene.object_count > 0) {
                        MessageBox(g_window_handle, "Model Mode only supports one object. Use 'New Model' to start over.", "Action Blocked", MB_OK | MB_ICONINFORMATION);
                        break;
                    }
                    scene_add_object(&g_scene, g_edge_mesh_data, new_pos);
                    break;
                case ID_ADD_VERTEX:
                    if (g_current_editor_mode == EDITOR_MODEL && g_scene.object_count > 0) {
                        MessageBox(g_window_handle, "Model Mode only supports one object. Use 'New Model' to start over.", "Action Blocked", MB_OK | MB_ICONINFORMATION);
                        break;
                    }
                    scene_add_object(&g_scene, g_vertex_mesh_data, new_pos);
                    break;
                case ID_ADD_POINT_LIGHT: scene_add_light(&g_scene, LIGHT_TYPE_POINT, new_pos); break;
                case ID_ADD_SPOT_LIGHT: scene_add_light(&g_scene, LIGHT_TYPE_SPOT, new_pos); break;
                case ID_PARENT_OBJECT:
                    if (g_current_mode == MODE_OBJECT && g_selected_objects.count > 1) {
                        int parent_index = g_selected_objects.items[g_selected_objects.count - 1];
                        for (int i = 0; i < g_selected_objects.count - 1; i++) {
                            int child_index = g_selected_objects.items[i];
                            scene_set_parent(&g_scene, child_index, parent_index);
                        }
                    }
                    break;
                case ID_UNPARENT_OBJECT:
                    if (g_current_mode == MODE_OBJECT && g_selected_objects.count > 0) {
                        for (int i = 0; i < g_selected_objects.count; i++) {
                            scene_set_parent(&g_scene, g_selected_objects.items[i], -1);
                        }
                    }
                    break;
                case ID_TOGGLE_DOUBLE_SIDED:
                    if (g_selected_objects.count > 0) {
                        int new_state = !g_scene.objects[g_selected_objects.items[0]]->is_double_sided;
                        for (int i = 0; i < g_selected_objects.count; i++) {
                            if(g_scene.objects[g_selected_objects.items[i]]->mesh)
                                g_scene.objects[g_selected_objects.items[i]]->is_double_sided = new_state;
                        }
                    }
                    break;
                case ID_TOGGLE_COLLISION: 
                    if (g_selected_objects.count > 0) {
                        int new_state = !g_scene.objects[g_selected_objects.items[0]]->has_collision;
                        for (int i = 0; i < g_selected_objects.count; i++) {
                            if (g_scene.objects[g_selected_objects.items[i]]->mesh) {
                                g_scene.objects[g_selected_objects.items[i]]->has_collision = new_state;
                            }
                        }
                    }
                    break;
                case ID_TOGGLE_STATIC:
                    if (g_selected_objects.count > 0) {
                        int new_state = !g_scene.objects[g_selected_objects.items[0]]->is_static;
                        for (int i = 0; i < g_selected_objects.count; i++) {
                            g_scene.objects[g_selected_objects.items[i]]->is_static = new_state;
                        }
                    }
                    break;
                case ID_SET_PLAYER_MODEL:
                    if (g_selected_objects.count == 1) {
                        int selected_idx = g_selected_objects.items[0];
                        // Toggle the state
                        int new_state = !g_scene.objects[selected_idx]->is_player_model;
                        
                        // If we are setting this object as the player model,
                        // we must first unset any other object that might have the flag.
                        if (new_state == 1) {
                            for (int i = 0; i < g_scene.object_count; i++) {
                                g_scene.objects[i]->is_player_model = 0;
                            }
                        }
                        g_scene.objects[selected_idx]->is_player_model = new_state;
                    }
                    break;
                case ID_SET_CAMERA_TARGET:
                    if (g_selected_objects.count == 1 && g_current_mode == MODE_EDIT && g_edit_mode_component == EDIT_VERTICES && g_selected_components.count > 0) {
                        scene_object_t* obj = g_scene.objects[g_selected_objects.items[0]];
                        
                        // Calculate the average position of the selected vertices
                        vec3_t center = {0, 0, 0};
                        for (int i = 0; i < g_selected_components.count; i++) {
                            int vert_idx = g_selected_components.items[i];
                            center = vec3_add(center, obj->mesh->vertices[vert_idx]);
                        }
                        obj->camera_offset = vec3_scale(center, 1.0f / g_selected_components.count);
                    }
                    break;
            }
        } break;
        case WM_MOUSEMOVE: {
            int cur_x=LOWORD(l_param), cur_y=HIWORD(l_param), dx=cur_x-g_last_mouse_x, dy=cur_y-g_last_mouse_y;
            
            if (g_current_tool == TOOL_DRAW_FACE && g_mouse_down) {
                g_mouse_dragged = 1;
                
                vec3_t world_pos = get_world_pos_on_plane(cur_x, cur_y);
                world_pos.x = roundf(world_pos.x);
                world_pos.y = roundf(world_pos.y);
                world_pos.z = roundf(world_pos.z);

                if (world_pos.x != g_last_tile_pos.x || world_pos.y != g_last_tile_pos.y || world_pos.z != g_last_tile_pos.z) {
                    create_grid_face_at(world_pos);
                    g_last_tile_pos = world_pos;
                }
            }
            else if (g_is_box_selecting) {
                g_mouse_dragged = 1;
                // --- MODIFIED: Scale current mouse position to render space ---
                float scaled_mx = (float)cur_x * ((float)g_render_width / (float)g_window_width);
                float scaled_my = (float)cur_y * ((float)g_render_height / (float)g_window_height);
                g_selection_box_rect.right = (int)scaled_mx;
                g_selection_box_rect.bottom = (int)scaled_my;
            } else if (g_current_transform_mode != TRANSFORM_NONE) {
                if(g_selected_objects.count == 0) break;
                
                int dx_total = cur_x - g_transform_start_mouse_pos.x;
                int dy_total = cur_y - g_transform_start_mouse_pos.y;
                
                float sens_translate = 0.002f * g_camera_distance;
                float sens_rotate = 0.01f;
                float sens_scale = 0.01f;
                float sens_intensity = 0.1f;

                if (g_current_mode == MODE_OBJECT) {
                    for (int i = 0; i < g_selected_objects.count; i++) {
                        scene_object_t* obj = g_scene.objects[g_selected_objects.items[i]];
                        
                        switch(g_current_transform_mode) {
                            case TRANSFORM_GRAB: {
                                vec3_t mv={0,0,0};
                                if (g_transform_axis_is_locked) {
                                    mv = vec3_scale(g_transform_axis, (float)(dx_total - dy_total) * sens_translate);
                                } else {
                                    vec3_t offset;
                                    offset.x = g_camera_distance * cosf(g_camera_pitch) * cosf(g_camera_yaw);
                                    offset.y = g_camera_distance * cosf(g_camera_pitch) * sinf(g_camera_yaw);
                                    offset.z = g_camera_distance * sinf(g_camera_pitch);
                                    vec3_t cam_pos=vec3_add(g_camera_target,offset); 
                                    vec3_t fwd=vec3_normalize(vec3_sub(g_camera_target,cam_pos)); 
                                    vec3_t right=vec3_normalize(vec3_cross(fwd,(vec3_t){0,0,1})); 
                                    vec3_t up=vec3_normalize(vec3_cross(right,fwd)); 
                                    mv=vec3_add(vec3_scale(right, (float)dx_total * sens_translate), vec3_scale(up, (float)-dy_total * sens_translate));
                                }
                                obj->position = vec3_add(g_transform_initial_position, mv);
                            } break;

                            case TRANSFORM_ROTATE: {
                                vec3_t rot_delta = {0};
                                if (g_transform_axis_is_locked) {
                                    rot_delta = vec3_scale(g_transform_axis, (float)dx_total * sens_rotate);
                                } else {
                                    rot_delta.x = (float)dy_total * sens_rotate;
                                    rot_delta.y = (float)dx_total * sens_rotate;
                                }
                                obj->rotation = vec3_add(g_transform_initial_rotation, rot_delta);
                            } break;
                            
                            case TRANSFORM_SCALE: {
                                float scale_factor = 1.0f + (float)dx_total * sens_scale;
                                if (scale_factor < 0.01f) scale_factor = 0.01f;
                                
                                vec3_t current_scale = g_transform_initial_scale;
                                if (g_transform_axis_is_locked) {
                                     if (g_transform_axis.x != 0) current_scale.x *= scale_factor;
                                     else if (g_transform_axis.y != 0) current_scale.y *= scale_factor;
                                     else if (g_transform_axis.z != 0) current_scale.z *= scale_factor;
                                } else { 
                                    current_scale = vec3_scale(g_transform_initial_scale, scale_factor);
                                }
                                obj->scale = current_scale;
                            } break;
                            case TRANSFORM_LIGHT_INTENSITY: {
                                if (obj->light_properties) {
                                    float new_intensity = g_transform_initial_intensity - (float)dy_total * sens_intensity;
                                    if (new_intensity < 0) new_intensity = 0;
                                    obj->light_properties->intensity = new_intensity;
                                }
                            } break;
                            case TRANSFORM_NONE: break;
                        }
                    }
                } else { // MODE_EDIT
                    scene_object_t* obj = g_scene.objects[g_selected_objects.items[0]];
                    
                    switch (g_current_transform_mode) {
                        case TRANSFORM_GRAB: {
                            vec3_t mv={0,0,0};
                            if (g_transform_axis_is_locked) {
                                mv = vec3_scale(g_transform_axis, (float)(dx_total - dy_total) * sens_translate);
                            } else {
                                vec3_t offset;
                                offset.x = g_camera_distance * cosf(g_camera_pitch) * cosf(g_camera_yaw);
                                offset.y = g_camera_distance * cosf(g_camera_pitch) * sinf(g_camera_yaw);
                                offset.z = g_camera_distance * sinf(g_camera_pitch);
                                vec3_t cam_pos=vec3_add(g_camera_target,offset); 
                                vec3_t fwd=vec3_normalize(vec3_sub(g_camera_target,cam_pos)); 
                                vec3_t right=vec3_normalize(vec3_cross(fwd,(vec3_t){0,0,1})); 
                                vec3_t up=vec3_normalize(vec3_cross(right,fwd)); 
                                mv=vec3_add(vec3_scale(right, (float)dx_total * sens_translate), vec3_scale(up, (float)-dy_total * sens_translate));
                            }
                            
                            int* processed_verts = (int*)calloc(obj->mesh->vertex_count, sizeof(int));
                            if(!processed_verts) break;

                            for (int i=0; i < g_selected_components.count; i++) {
                                int comp_idx = g_selected_components.items[i];
                                if (g_edit_mode_component==EDIT_VERTICES){
                                    if(!processed_verts[comp_idx]) {
                                        obj->mesh->vertices[comp_idx] = vec3_add(g_transform_initial_vertices[comp_idx], mv);
                                        processed_verts[comp_idx] = 1;
                                    }
                                } else if (g_edit_mode_component==EDIT_EDGES){
                                    int v_indices[2] = {(comp_idx>>16)&0xFFFF, comp_idx&0xFFFF};
                                    for(int v=0; v<2; v++) if(!processed_verts[v_indices[v]]) {
                                        obj->mesh->vertices[v_indices[v]] = vec3_add(g_transform_initial_vertices[v_indices[v]], mv);
                                        processed_verts[v_indices[v]] = 1;
                                    }
                                } else if (g_edit_mode_component==EDIT_FACES){
                                    int v_indices[3] = {obj->mesh->faces[comp_idx*3+0], obj->mesh->faces[comp_idx*3+1], obj->mesh->faces[comp_idx*3+2]};
                                    for(int v=0; v<3; v++) if(!processed_verts[v_indices[v]]) {
                                        obj->mesh->vertices[v_indices[v]] = vec3_add(g_transform_initial_vertices[v_indices[v]], mv);
                                        processed_verts[v_indices[v]] = 1;
                                    }
                                }
                            }
                            free(processed_verts);
                        } break;

                        case TRANSFORM_ROTATE: {
                            vec3_t pivot = get_selection_center(obj);
                            vec3_t rot_delta = {0};
                            if (g_transform_axis_is_locked) {
                                rot_delta = vec3_scale(g_transform_axis, (float)dx_total * sens_rotate);
                            } else {
                                rot_delta.y = (float)dx_total * sens_rotate;
                                rot_delta.x = (float)dy_total * sens_rotate;
                            }
                            mat4_t rot_x_m = mat4_rotation_x(rot_delta.x);
                            mat4_t rot_y_m = mat4_rotation_y(rot_delta.y);
                            mat4_t rot_z_m = mat4_rotation_z(rot_delta.z);
                            mat4_t rot_matrix = mat4_mul_mat4(rot_z_m, mat4_mul_mat4(rot_y_m, rot_x_m));

                            int* processed_verts = (int*)calloc(obj->mesh->vertex_count, sizeof(int));
                            if(!processed_verts) break;

                            for (int i = 0; i < g_selected_components.count; i++) {
                                int comp_idx = g_selected_components.items[i];
                                if (g_edit_mode_component==EDIT_VERTICES){
                                    if(!processed_verts[comp_idx]){
                                        vec3_t p = vec3_sub(g_transform_initial_vertices[comp_idx], pivot);
                                        vec4_t p4 = mat4_mul_vec4(rot_matrix, (vec4_t){p.x, p.y, p.z, 1.0f});
                                        obj->mesh->vertices[comp_idx] = vec3_add((vec3_t){p4.x, p4.y, p4.z}, pivot);
                                        processed_verts[comp_idx] = 1;
                                    }
                                } else if (g_edit_mode_component==EDIT_EDGES){
                                    int v_indices[2] = {(comp_idx>>16)&0xFFFF, comp_idx&0xFFFF};
                                    for(int v=0; v<2; v++) if(!processed_verts[v_indices[v]]) {
                                        vec3_t p = vec3_sub(g_transform_initial_vertices[v_indices[v]], pivot);
                                        vec4_t p4 = mat4_mul_vec4(rot_matrix, (vec4_t){p.x, p.y, p.z, 1.0f});
                                        obj->mesh->vertices[v_indices[v]] = vec3_add((vec3_t){p4.x, p4.y, p4.z}, pivot);
                                        processed_verts[v_indices[v]] = 1;
                                    }
                                } else if (g_edit_mode_component==EDIT_FACES){
                                    int v_indices[3] = {obj->mesh->faces[comp_idx*3+0], obj->mesh->faces[comp_idx*3+1], obj->mesh->faces[comp_idx*3+2]};
                                    for(int v=0; v<3; v++) if(!processed_verts[v_indices[v]]) {
                                        vec3_t p = vec3_sub(g_transform_initial_vertices[v_indices[v]], pivot);
                                        vec4_t p4 = mat4_mul_vec4(rot_matrix, (vec4_t){p.x, p.y, p.z, 1.0f});
                                        obj->mesh->vertices[v_indices[v]] = vec3_add((vec3_t){p4.x, p4.y, p4.z}, pivot);
                                        processed_verts[v_indices[v]] = 1;
                                    }
                                }
                            }
                            free(processed_verts);
                        } break;

                        case TRANSFORM_SCALE: {
                            vec3_t pivot = get_selection_center(obj);
                            float scale_factor = 1.0f + (float)dx_total * sens_scale;
                            if (scale_factor < 0.01f) scale_factor = 0.01f;

                            int* processed_verts = (int*)calloc(obj->mesh->vertex_count, sizeof(int));
                            if(!processed_verts) break;
                            
                            for (int i = 0; i < g_selected_components.count; i++) {
                                int comp_idx = g_selected_components.items[i];
                                if (g_edit_mode_component==EDIT_VERTICES){
                                    if(!processed_verts[comp_idx]){
                                        vec3_t p = vec3_sub(g_transform_initial_vertices[comp_idx], pivot);
                                        p = vec3_scale(p, scale_factor);
                                        obj->mesh->vertices[comp_idx] = vec3_add(p, pivot);
                                        processed_verts[comp_idx] = 1;
                                    }
                                } else if (g_edit_mode_component==EDIT_EDGES){
                                    int v_indices[2] = {(comp_idx>>16)&0xFFFF, comp_idx&0xFFFF};
                                    for(int v=0; v<2; v++) if(!processed_verts[v_indices[v]]) {
                                        vec3_t p = vec3_sub(g_transform_initial_vertices[v_indices[v]], pivot);
                                        p = vec3_scale(p, scale_factor);
                                        obj->mesh->vertices[v_indices[v]] = vec3_add(p, pivot);
                                        processed_verts[v_indices[v]] = 1;
                                    }
                                } else if (g_edit_mode_component==EDIT_FACES){
                                    int v_indices[3] = {obj->mesh->faces[comp_idx*3+0], obj->mesh->faces[comp_idx*3+1], obj->mesh->faces[comp_idx*3+2]};
                                    for(int v=0; v<3; v++) if(!processed_verts[v_indices[v]]) {
                                        vec3_t p = vec3_sub(g_transform_initial_vertices[v_indices[v]], pivot);
                                        p = vec3_scale(p, scale_factor);
                                        obj->mesh->vertices[v_indices[v]] = vec3_add(p, pivot);
                                        processed_verts[v_indices[v]] = 1;
                                    }
                                }
                            }
                            free(processed_verts);
                        } break;
                        case TRANSFORM_NONE: break;
                        case TRANSFORM_LIGHT_INTENSITY: break;
                    }
                }
            } else if (g_middle_mouse_down && (GetKeyState(VK_SHIFT) & 0x8000)) {
                g_mouse_dragged=1; float sens=0.0025f*g_camera_distance;
                vec3_t offset;
                offset.x = g_camera_distance * cosf(g_camera_pitch) * cosf(g_camera_yaw);
                offset.y = g_camera_distance * cosf(g_camera_pitch) * sinf(g_camera_yaw);
                offset.z = g_camera_distance * sinf(g_camera_pitch);
                vec3_t cam_pos=vec3_add(g_camera_target,offset); 
                vec3_t fwd=vec3_normalize(vec3_sub(g_camera_target,cam_pos)); 
                vec3_t right=vec3_normalize(vec3_cross(fwd,(vec3_t){0,0,1})); 
                vec3_t up=vec3_normalize(vec3_cross(right,fwd));
                g_camera_target=vec3_add(g_camera_target,vec3_add(vec3_scale(right,-dx*sens),vec3_scale(up,dy*sens)));
            } else if (g_middle_mouse_down) {
                g_mouse_dragged=1; g_camera_yaw+=(float)dx*0.01f; g_camera_pitch+=(float)dy*0.01f;
                if(g_camera_pitch>1.5f)g_camera_pitch=1.5f; if(g_camera_pitch<-1.5f)g_camera_pitch=-1.5f;
            } else if (g_mouse_down) { g_mouse_dragged=1; }
            g_last_mouse_x=cur_x; g_last_mouse_y=cur_y;
        } break;
        case WM_KEYDOWN: {
            if ((GetKeyState(VK_CONTROL) & 0x8000)) {
                switch(w_param) {
                    case 'S': 
                        if (g_current_editor_mode == EDITOR_SCENE) trigger_save_scene_dialog();
                        else trigger_save_model_dialog();
                        return 0;
                    case 'O': 
                        if (g_current_editor_mode == EDITOR_SCENE) trigger_load_scene_dialog();
                        else trigger_load_model_dialog();
                        return 0;
                }
            }
            
            if (g_current_transform_mode != TRANSFORM_NONE) {
                switch(w_param){
                    case 'X': g_transform_axis = (vec3_t){1,0,0}; g_transform_axis_is_locked = 1; break;
                    case 'Y': g_transform_axis = (vec3_t){0,1,0}; g_transform_axis_is_locked = 1; break;
                    case 'Z': g_transform_axis = (vec3_t){0,0,1}; g_transform_axis_is_locked = 1; break;
                    case VK_ESCAPE: {
                        if (g_selected_objects.count > 0) {
                             scene_object_t* obj = g_scene.objects[g_selected_objects.items[0]];
                            if (g_current_mode == MODE_OBJECT) {
                                if (obj->light_properties && g_current_transform_mode == TRANSFORM_LIGHT_INTENSITY) {
                                    obj->light_properties->intensity = g_transform_initial_intensity;
                                } else {
                                    obj->position = g_transform_initial_position;
                                    obj->rotation = g_transform_initial_rotation;
                                    obj->scale    = g_transform_initial_scale;
                                }
                            } else if (g_current_mode == MODE_EDIT && g_transform_initial_vertices) {
                                memcpy(obj->mesh->vertices, g_transform_initial_vertices, obj->mesh->vertex_count * sizeof(vec3_t));
                                free(g_transform_initial_vertices);
                                g_transform_initial_vertices = NULL;
                            }
                        }
                        g_current_transform_mode = TRANSFORM_NONE;
                        g_transform_axis_is_locked = 0;
                    } break;
                }
                return 0;
            }
            
            if (w_param == 'X') {
                if (g_current_mode == MODE_EDIT && g_selected_objects.count > 0 && g_selected_components.count > 0) {
                    scene_object_t* object = g_scene.objects[g_selected_objects.items[0]];
                    mesh_t* mesh = object->mesh;

                    if (g_edit_mode_component == EDIT_FACES) {
                        qsort(g_selected_components.items, g_selected_components.count, sizeof(int), compare_ints_desc);
                        for (int i = 0; i < g_selected_components.count; i++) {
                            mesh_delete_face(mesh, g_selected_components.items[i]);
                        }
                        selection_clear(&g_selected_components);
                    }
                    return 0;
                }
            }

            if (w_param == 'D' && (GetKeyState(VK_SHIFT) & 0x8000)) {
                if (g_current_mode == MODE_OBJECT && g_selected_objects.count > 0 && g_current_transform_mode == TRANSFORM_NONE) {
                    selection_t new_selection;
                    selection_init(&new_selection);

                    for (int i = 0; i < g_selected_objects.count; i++) {
                        int source_idx = g_selected_objects.items[i];
                        int new_idx = scene_duplicate_object(&g_scene, source_idx);
                        if (new_idx != -1) {
                            selection_add(&new_selection, new_idx);
                        }
                    }

                    if (new_selection.count > 0) {
                        selection_destroy(&g_selected_objects);
                        g_selected_objects = new_selection;

                        g_current_transform_mode = TRANSFORM_GRAB;
                        g_transform_axis_is_locked = 0;
                        g_transform_axis = (vec3_t){0,0,0};
                        GetCursorPos(&g_transform_start_mouse_pos);
                        ScreenToClient(window_handle, &g_transform_start_mouse_pos);
                        
                        scene_object_t* first_new_obj = g_scene.objects[g_selected_objects.items[0]];
                        g_transform_initial_position = first_new_obj->position;
                        g_transform_initial_rotation = first_new_obj->rotation;
                        g_transform_initial_scale    = first_new_obj->scale;
                        
                        if(g_transform_initial_vertices) {
                            free(g_transform_initial_vertices);
                            g_transform_initial_vertices = NULL;
                        }
                    } else {
                        selection_destroy(&new_selection);
                    }
                    return 0;
                }
            }

            switch(w_param) {
                case VK_NUMPAD1: case VK_END:
                    g_camera_yaw = 0.0f; g_camera_pitch = 0.0f; g_is_orthographic = 1; break;
                case VK_NUMPAD3: case VK_NEXT:
                    g_camera_yaw = 1.570796f; g_camera_pitch = 0.0f; g_is_orthographic = 1; break;
                case VK_NUMPAD7: case VK_HOME:
                    g_camera_yaw = 0.0f; g_camera_pitch = 1.5f; g_is_orthographic = 1; break;
                case VK_NUMPAD5: case VK_CLEAR:
                    g_is_orthographic = !g_is_orthographic; break;
            }

            if (w_param==VK_TAB) {
                if (g_current_mode==MODE_OBJECT && g_selected_objects.count > 0 && g_scene.objects[g_selected_objects.items[0]]->mesh) {
                    g_current_mode=MODE_EDIT;
                    g_edit_mode_component=EDIT_FACES;
                    selection_clear(&g_selected_components);
                } else {
                    g_current_mode=MODE_OBJECT;
                    selection_clear(&g_selected_components);
                }
            }
            if(g_current_mode==MODE_EDIT){
                switch(w_param){
                    case '1':g_edit_mode_component=EDIT_FACES;selection_clear(&g_selected_components);break;
                    case '2':g_edit_mode_component=EDIT_VERTICES;selection_clear(&g_selected_components);break;
                    case '3':g_edit_mode_component=EDIT_EDGES;selection_clear(&g_selected_components);break;
                }
            }
            
            if (w_param == 'G' || w_param == 'R' || w_param == 'S') {
                int is_shift_down = GetKeyState(VK_SHIFT) & 0x8000;
                
                int can_transform = (g_selected_objects.count > 0) && (g_current_mode == MODE_OBJECT || (g_current_mode == MODE_EDIT && g_selected_components.count > 0));
                
                if (can_transform && g_current_editor_mode == EDITOR_SCENE && g_current_mode == MODE_OBJECT) {
                    for (int i = 0; i < g_selected_objects.count; i++) {
                        if (g_scene.objects[g_selected_objects.items[i]]->is_static) {
                            can_transform = 0;
                            break;
                        }
                    }
                }
                
                scene_object_t* first_obj = can_transform ? g_scene.objects[g_selected_objects.items[0]] : NULL;

                if (can_transform) {
                    if (w_param == 'G') g_current_transform_mode = TRANSFORM_GRAB;
                    else if (w_param == 'R') g_current_transform_mode = TRANSFORM_ROTATE;
                    else if (w_param == 'S') {
                        if (first_obj->light_properties && !is_shift_down) {
                            g_current_transform_mode = TRANSFORM_LIGHT_INTENSITY;
                        } else {
                            g_current_transform_mode = TRANSFORM_SCALE;
                        }
                    }

                    g_transform_axis_is_locked = 0;
                    g_transform_axis = (vec3_t){0,0,0};
                    GetCursorPos(&g_transform_start_mouse_pos);
                    ScreenToClient(window_handle, &g_transform_start_mouse_pos);
                    
                    if (g_current_mode == MODE_OBJECT) {
                        g_transform_initial_position = first_obj->position;
                        g_transform_initial_rotation = first_obj->rotation;
                        g_transform_initial_scale    = first_obj->scale;
                        if(first_obj->light_properties) {
                             g_transform_initial_intensity = first_obj->light_properties->intensity;
                        }
                    } else { 
                        int vc = first_obj->mesh->vertex_count;
                        if(g_transform_initial_vertices) free(g_transform_initial_vertices);
                        g_transform_initial_vertices = (vec3_t*)malloc(vc * sizeof(vec3_t));
                        if (g_transform_initial_vertices) {
                            memcpy(g_transform_initial_vertices, first_obj->mesh->vertices, vc * sizeof(vec3_t));
                        }
                    }
                }
            }

            if (w_param == 'F') {
                if (g_current_mode == MODE_EDIT && g_edit_mode_component == EDIT_VERTICES && g_selected_objects.count > 0 && g_selected_components.count >= 3) {
                    scene_object_t* object = g_scene.objects[g_selected_objects.items[0]];
                    int root_vertex_idx = g_selected_components.items[0];
                    for (int i = 1; i < g_selected_components.count - 1; i++) {
                        int v2_idx = g_selected_components.items[i];
                        int v3_idx = g_selected_components.items[i + 1];
                        mesh_add_face(object->mesh, root_vertex_idx, v2_idx, v3_idx);
                    }
                    selection_clear(&g_selected_components);
                }
            }
            if (w_param == 'E') {
                if (g_current_mode == MODE_EDIT && g_selected_objects.count > 0 && g_selected_components.count > 0) {
                    scene_object_t* object = g_scene.objects[g_selected_objects.items[0]];
                    mesh_t* mesh = object->mesh;
                    
                    int* old_to_new_map = (int*)malloc(mesh->vertex_count * sizeof(int));
                    if (!old_to_new_map) break;
                    memset(old_to_new_map, -1, mesh->vertex_count * sizeof(int));

                    if (g_edit_mode_component == EDIT_FACES) {
                        selection_t unique_verts; selection_init(&unique_verts);
                        for (int i = 0; i < g_selected_components.count; i++) {
                            int face_idx = g_selected_components.items[i];
                            for (int j = 0; j < 3; j++) selection_add(&unique_verts, mesh->faces[face_idx * 3 + j]);
                        }
                        for (int i = 0; i < unique_verts.count; i++) {
                            int v_old = unique_verts.items[i];
                            old_to_new_map[v_old] = mesh_add_vertex(mesh, mesh->vertices[v_old]);
                        }
                        edge_record_t* edges = NULL; int edge_count = 0, edge_capacity = 0;
                        for (int i = 0; i < g_selected_components.count; i++) {
                            int face_verts[3] = {mesh->faces[g_selected_components.items[i]*3], mesh->faces[g_selected_components.items[i]*3+1], mesh->faces[g_selected_components.items[i]*3+2]};
                            for(int j=0; j<3; j++) {
                                int v1=face_verts[j], v2=face_verts[(j+1)%3], min_v=(v1<v2)?v1:v2, max_v=(v1>v2)?v1:v2, found=0;
                                for(int k=0; k<edge_count; k++) if(edges[k].v1==min_v&&edges[k].v2==max_v){edges[k].count++;found=1;break;}
                                if(!found) {
                                    if(edge_count>=edge_capacity){edge_capacity=(edge_capacity==0)?10:edge_capacity*2; edges=(edge_record_t*)realloc(edges,edge_capacity*sizeof(edge_record_t));}
                                    edges[edge_count++]=(edge_record_t){min_v, max_v, 1};
                                }
                            }
                        }
                        for (int i=0;i<edge_count;i++) if(edges[i].count==1){mesh_add_face(mesh,edges[i].v1,edges[i].v2,old_to_new_map[edges[i].v2]); mesh_add_face(mesh,old_to_new_map[edges[i].v2],old_to_new_map[edges[i].v1],edges[i].v1);}
                        if(edges) free(edges);
                        selection_t new_face_selection; selection_init(&new_face_selection);
                        for(int i=0;i<g_selected_components.count;i++){mesh_add_face(mesh,old_to_new_map[mesh->faces[g_selected_components.items[i]*3]], old_to_new_map[mesh->faces[g_selected_components.items[i]*3+1]], old_to_new_map[mesh->faces[g_selected_components.items[i]*3+2]]); selection_add(&new_face_selection, mesh->face_count-1);}
                        selection_destroy(&g_selected_components); g_selected_components = new_face_selection;
                        selection_destroy(&unique_verts);
                    } else if (g_edit_mode_component == EDIT_VERTICES) {
                        selection_t new_vert_selection; selection_init(&new_vert_selection);
                        for (int i = 0; i < g_selected_components.count; i++) {
                            int v_old_idx=g_selected_components.items[i], v_new_idx=mesh_add_vertex(mesh,mesh->vertices[v_old_idx]);
                            old_to_new_map[v_old_idx] = v_new_idx; selection_add(&new_vert_selection, v_new_idx);
                        }
                        if (g_selected_components.count == 2) {int v1_old=g_selected_components.items[0],v2_old=g_selected_components.items[1],v1_new=old_to_new_map[v1_old],v2_new=old_to_new_map[v2_old]; mesh_add_face(mesh,v1_old,v2_old,v2_new); mesh_add_face(mesh,v2_new,v1_new,v1_old);}
                        selection_destroy(&g_selected_components); g_selected_components = new_vert_selection;
                    } else if (g_edit_mode_component == EDIT_EDGES) {
                        selection_t unique_verts; selection_init(&unique_verts);
                        for (int i = 0; i < g_selected_components.count; i++) {int packed=g_selected_components.items[i]; selection_add(&unique_verts,(packed>>16)&0xFFFF); selection_add(&unique_verts,packed&0xFFFF);}
                        for (int i = 0; i < unique_verts.count; i++) {int v_old_idx=unique_verts.items[i]; old_to_new_map[v_old_idx]=mesh_add_vertex(mesh,mesh->vertices[v_old_idx]);}
                        selection_t new_edge_selection; selection_init(&new_edge_selection);
                        for (int i = 0; i < g_selected_components.count; i++) {
                            int v1_old=(g_selected_components.items[i]>>16)&0xFFFF, v2_old=g_selected_components.items[i]&0xFFFF, v1_new=old_to_new_map[v1_old], v2_new=old_to_new_map[v2_old];
                            if(v1_new!=-1&&v2_new!=-1){mesh_add_face(mesh,v1_old,v2_old,v2_new); mesh_add_face(mesh,v2_new,v1_new,v1_old); int min_v=(v1_new<v2_new)?v1_new:v2_new, max_v=(v1_new>v2_new)?v1_new:v2_new; selection_add(&new_edge_selection,(min_v<<16)|max_v);}
                        }
                        selection_destroy(&g_selected_components); g_selected_components=new_edge_selection;
                        selection_destroy(&unique_verts);
                    }
                    free(old_to_new_map);
                    
                    g_current_transform_mode = TRANSFORM_GRAB;
                    g_transform_axis_is_locked = 0;
                    g_transform_axis = (vec3_t){0,0,0};
                    GetCursorPos(&g_transform_start_mouse_pos);
                    ScreenToClient(window_handle, &g_transform_start_mouse_pos);
                    int vc = mesh->vertex_count;
                    if(g_transform_initial_vertices) free(g_transform_initial_vertices);
                    g_transform_initial_vertices = (vec3_t*)malloc(vc * sizeof(vec3_t));
                    if (g_transform_initial_vertices) {
                        memcpy(g_transform_initial_vertices, mesh->vertices, vc * sizeof(vec3_t));
                    }
                }
            }
            if (g_current_mode==MODE_OBJECT && g_selected_objects.count > 0) {
                float ms=0.1f, rs=0.1f; int shift=GetKeyState(VK_SHIFT)&0x8000;
                for (int i=0; i<g_selected_objects.count; i++) {
                    scene_object_t* obj = g_scene.objects[g_selected_objects.items[i]];
                    if (g_current_editor_mode == EDITOR_SCENE && obj->is_static) continue;
                    switch(w_param){
                        case 'W':shift?(obj->rotation.y-=rs):(obj->position.y+=ms);break; 
                        case 'S':shift?(obj->rotation.y+=rs):(obj->position.y-=ms);break;
                        case 'A':shift?(obj->rotation.x-=rs):(obj->position.x-=ms);break; 
                        case 'D':shift?(obj->rotation.x+=rs):(obj->position.x+=ms);break;
                        case 'E':shift?(obj->rotation.z+=rs):(obj->position.z+=ms);break; 
                        case 'Q':shift?(obj->rotation.z-=rs):(obj->position.z-=ms);break;
                    }
                }
            }
        } break;
        default: { result = DefWindowProc(window_handle, message, w_param, l_param); }
    }
    return result;
}
