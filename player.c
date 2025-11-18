//gcc player.c math3d.c -o player.exe -lgdi32 -luser32 -lcomdlg32 -lmsimg32

#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <float.h>
#include <stdio.h>
#include "math3d.h"
#include <math.h>

typedef struct {
    vec4_t vertices[3];
    vec3_t colors[3];
} triangle_t;
typedef enum {
    GAME_RUNNING,
    GAME_PAUSED
} game_state_t;

typedef struct {
    float fov_degrees;
    float mouse_sensitivity;
} player_config_t;

// --- Global Variables ---
static game_state_t g_game_state = GAME_RUNNING;
static player_config_t g_player_config;

// --- Pause Menu UI Rects ---
static RECT g_resume_button_rect;
static RECT g_exit_button_rect;
static RECT g_fov_minus_rect, g_fov_plus_rect;
static RECT g_sens_minus_rect, g_sens_plus_rect;
// --- Global Variables ---
static HWND g_window_handle;
static BITMAPINFO g_framebuffer_info;
static void* g_framebuffer_memory;
static float* g_depth_buffer;
static int g_window_width;
static int g_window_height;
static int g_render_width;
static int g_render_height;
static uint32_t g_sky_color_uint = 0xFF303030; // Default dark grey

// Camera and Mouse Input Variables
static float g_camera_distance = 6.0f;
static float g_camera_yaw = 0.0f;
static float g_camera_pitch = 0.3f;
static vec3_t g_camera_target = {0.0f, 0.0f, 0.0f};
static int g_middle_mouse_down = 0;
static int g_last_mouse_x = 0;
static int g_last_mouse_y = 0;

// Scene Management
static scene_t g_scene;
static LARGE_INTEGER g_perf_counter_freq; // For delta time calculation
static LARGE_INTEGER g_last_perf_counter;
// --- Player State Variables ---
static vec3_t g_player_position = {0, 0, 1}; // Player's current world position
static vec3_t g_player_velocity = {0, 0, 0}; // Player's current velocity
static float g_player_yaw = 0.0f;           // Horizontal look angle (around Z axis)
static float g_player_pitch = 0.0f;         // Vertical look angle
static int g_player_on_ground = 0;          // Is the player currently standing on a surface?
static int g_player_active = 0;             // Is the player logic running? (Set after finding spawn)
static int g_mouse_is_free = 0;             // 0 = Locked for FPS control, 1 = Free
static int g_player_model_index = -1;       // Index of the object used as the player model
static vec3_t g_last_player_position = {FLT_MAX, FLT_MAX, FLT_MAX};
static float g_last_player_yaw = FLT_MAX;
static float g_last_player_pitch = FLT_MAX;

#define PLAYER_HEIGHT 1.5f
#define PLAYER_EYE_HEIGHT 1.3f
#define PLAYER_RADIUS 0.3f
#define PLAYER_SPEED 5.0f
#define PLAYER_GRAVITY -18.0f
#define PLAYER_JUMP_FORCE 7.0f

// New physics constants
#define PLAYER_ACCELERATION 50.0f
#define PLAYER_AIR_ACCELERATION 5.0f
#define PLAYER_FRICTION 12.0f

static vec4_t* g_clip_coords_buffer = NULL;
static vec3_t* g_colors_buffer = NULL;
static int g_vertex_buffer_capacity = 0;
// --- Function Declarations ---
LRESULT CALLBACK window_callback(HWND, UINT, WPARAM, LPARAM);
void render_frame();
void draw_pixel(int, int, float, uint32_t);
void draw_line(int x0, int y0, float z0, int x1, int y1, float z1, uint32_t color);
void draw_gouraud_triangle(vec4_t p0, vec4_t p1, vec4_t p2, vec3_t c0, vec3_t c1, vec3_t c2);
void render_object(scene_object_t* object, int object_index, mat4_t view_matrix, mat4_t projection_matrix, vec3_t camera_pos);
void render_grid(mat4_t view_matrix, mat4_t projection_matrix);
void update_player(float dt);

// Scene I/O
void scene_init(scene_t* scene);
void scene_destroy(scene_t* scene);
int scene_load_from_file(scene_t* scene, const char* filename);
void destroy_mesh_data(mesh_t* mesh);
void mesh_calculate_normals(mesh_t* mesh);

// --- Scene Management ---
void mesh_calculate_normals(mesh_t* mesh) {
    if (!mesh || mesh->vertex_count == 0 || mesh->face_count == 0) {
        if (mesh && mesh->normals) {
            free(mesh->normals);
            mesh->normals = NULL;
        }
        return;
    }

    if (mesh->normals) {
        free(mesh->normals);
    }
    mesh->normals = (vec3_t*)calloc(mesh->vertex_count, sizeof(vec3_t));
    if (!mesh->normals) return;

    for (int i = 0; i < mesh->face_count; i++) {
        int v0_idx = mesh->faces[i * 3 + 0];
        int v1_idx = mesh->faces[i * 3 + 1];
        int v2_idx = mesh->faces[i * 3 + 2];

        // Ensure indices are within bounds
        if (v0_idx >= mesh->vertex_count || v1_idx >= mesh->vertex_count || v2_idx >= mesh->vertex_count) continue;

        vec3_t v0 = mesh->vertices[v0_idx];
        vec3_t v1 = mesh->vertices[v1_idx];
        vec3_t v2 = mesh->vertices[v2_idx];

        vec3_t edge1 = vec3_sub(v1, v0);
        vec3_t edge2 = vec3_sub(v2, v0);
        vec3_t face_normal = vec3_cross(edge1, edge2);

        mesh->normals[v0_idx] = vec3_add(mesh->normals[v0_idx], face_normal);
        mesh->normals[v1_idx] = vec3_add(mesh->normals[v1_idx], face_normal);
        mesh->normals[v2_idx] = vec3_add(mesh->normals[v2_idx], face_normal);
    }

    for (int i = 0; i < mesh->vertex_count; i++) {
        mesh->normals[i] = vec3_normalize(mesh->normals[i]);
    }
}

void scene_init(scene_t* scene) {
    scene->capacity = 10;
    scene->object_count = 0;
    scene->objects = (scene_object_t**)malloc(scene->capacity * sizeof(scene_object_t*));
}

void destroy_mesh_data(mesh_t* mesh) {
    if (!mesh) return;
    if (mesh->vertices) free(mesh->vertices);
    if (mesh->faces) free(mesh->faces);
    if (mesh->normals) free(mesh->normals);
    free(mesh);
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
void save_config(const player_config_t* config) {
    FILE* file = fopen("player_config.dat", "wb");
    if (file) {
        fwrite(config, sizeof(player_config_t), 1, file);
        fclose(file);
    }
}
void load_config(player_config_t* config) {
    FILE* file = fopen("player_config.dat", "rb");
    if (file) {
        fread(config, sizeof(player_config_t), 1, file);
        fclose(file);
    } else {
        // Set default values if the config file doesn't exist
        config->fov_degrees = 90.0f;
        config->mouse_sensitivity = 0.0015f;
        save_config(config); // Create the file with default values
    }
}
void draw_pause_menu(HDC hdc) {
    // --- Draw Semi-Transparent Background ---
    HBRUSH hBrush = CreateSolidBrush(0x00000000); // Black brush
    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hbmMem = CreateCompatibleBitmap(hdc, g_render_width, g_render_height);
    HANDLE hOld = SelectObject(hdcMem, hbmMem);

    BitBlt(hdcMem, 0, 0, g_render_width, g_render_height, hdc, 0, 0, SRCCOPY);
    RECT panel_rect = { 0, 0, g_render_width, g_render_height };
    FillRect(hdcMem, &panel_rect, hBrush);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 128, 0 }; // 50% opacity
    AlphaBlend(hdc, 0, 0, g_render_width, g_render_height, hdcMem, 0, 0, g_render_width, g_render_height, bf);
    
    SelectObject(hdcMem, hOld);
    DeleteObject(hbmMem);
    DeleteDC(hdcMem);
    DeleteObject(hBrush);

    // --- Draw UI Elements ---
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, 0x00FFFFFF); // White text
    HFONT hFont = CreateFont(32, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, TEXT("Arial"));
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

    int center_x = g_render_width / 2;
    int y_pos = g_render_height / 2 - 150;
    char buffer[128];
    SIZE text_size;

    // --- FOV Setting ---
    sprintf_s(buffer, sizeof(buffer), "Field of View: %.0f", g_player_config.fov_degrees);
    GetTextExtentPoint32(hdc, buffer, strlen(buffer), &text_size);
    TextOut(hdc, center_x - text_size.cx / 2, y_pos, buffer, strlen(buffer));

    GetTextExtentPoint32(hdc, "-", 1, &text_size);
    g_fov_minus_rect.left = center_x - 150 - text_size.cx / 2;
    g_fov_minus_rect.right = g_fov_minus_rect.left + text_size.cx;
    g_fov_minus_rect.top = y_pos;
    g_fov_minus_rect.bottom = y_pos + text_size.cy;
    TextOut(hdc, g_fov_minus_rect.left, g_fov_minus_rect.top, "-", 1);

    GetTextExtentPoint32(hdc, "+", 1, &text_size);
    g_fov_plus_rect.left = center_x + 150 - text_size.cx / 2;
    g_fov_plus_rect.right = g_fov_plus_rect.left + text_size.cx;
    g_fov_plus_rect.top = y_pos;
    g_fov_plus_rect.bottom = y_pos + text_size.cy;
    TextOut(hdc, g_fov_plus_rect.left, g_fov_plus_rect.top, "+", 1);
    
    y_pos += 60;

    // --- Mouse Sensitivity Setting ---
    sprintf_s(buffer, sizeof(buffer), "Sensitivity: %.4f", g_player_config.mouse_sensitivity);
    GetTextExtentPoint32(hdc, buffer, strlen(buffer), &text_size);
    TextOut(hdc, center_x - text_size.cx / 2, y_pos, buffer, strlen(buffer));

    GetTextExtentPoint32(hdc, "-", 1, &text_size);
    g_sens_minus_rect.left = center_x - 150 - text_size.cx / 2;
    g_sens_minus_rect.right = g_sens_minus_rect.left + text_size.cx;
    g_sens_minus_rect.top = y_pos;
    g_sens_minus_rect.bottom = y_pos + text_size.cy;
    TextOut(hdc, g_sens_minus_rect.left, g_sens_minus_rect.top, "-", 1);

    GetTextExtentPoint32(hdc, "+", 1, &text_size);
    g_sens_plus_rect.left = center_x + 150 - text_size.cx / 2;
    g_sens_plus_rect.right = g_sens_plus_rect.left + text_size.cx;
    g_sens_plus_rect.top = y_pos;
    g_sens_plus_rect.bottom = y_pos + text_size.cy;
    TextOut(hdc, g_sens_plus_rect.left, g_sens_plus_rect.top, "+", 1);
    
    y_pos += 100;

    const char* resume_text = "Resume";
    GetTextExtentPoint32(hdc, resume_text, strlen(resume_text), &text_size);
    g_resume_button_rect.left = center_x - text_size.cx / 2;
    g_resume_button_rect.right = g_resume_button_rect.left + text_size.cx;
    g_resume_button_rect.top = y_pos;
    g_resume_button_rect.bottom = y_pos + text_size.cy;
    TextOut(hdc, g_resume_button_rect.left, g_resume_button_rect.top, resume_text, strlen(resume_text));

    y_pos += 60;
    
    const char* exit_text = "Exit";
    GetTextExtentPoint32(hdc, exit_text, strlen(exit_text), &text_size);
    g_exit_button_rect.left = center_x - text_size.cx / 2;
    g_exit_button_rect.right = g_exit_button_rect.left + text_size.cx;
    g_exit_button_rect.top = y_pos;
    g_exit_button_rect.bottom = y_pos + text_size.cy;
    TextOut(hdc, g_exit_button_rect.left, g_exit_button_rect.top, exit_text, strlen(exit_text));

    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);
}
int scene_load_from_file(scene_t* scene, const char* filename) {
    if (!scene || !filename) return 0;

    FILE* file = fopen(filename, "rb");
    if (!file) {
        return 0;
    }

    char header[4];
    fread(header, sizeof(char), 4, file);

    int is_scn4_format = (strncmp(header, "SCN4", 4) == 0);
    int is_scn3_format = (strncmp(header, "SCN3", 4) == 0);
    int is_scn2_format = (strncmp(header, "SCN2", 4) == 0);
    int is_scn1_format = (strncmp(header, "SCN1", 4) == 0);
    
    vec3_t sky_color_vec;
    int object_count = 0;

    if (is_scn4_format || is_scn3_format || is_scn2_format) {
        fread(&sky_color_vec, sizeof(vec3_t), 1, file);
        fread(&object_count, sizeof(int), 1, file);
    } else if (is_scn1_format) {
        sky_color_vec = (vec3_t){0.1875f, 0.1875f, 0.1875f}; 
        fread(&object_count, sizeof(int), 1, file);
    } else {
        sky_color_vec = (vec3_t){0.1875f, 0.1875f, 0.1875f};
        fseek(file, 0, SEEK_SET);
        fread(&object_count, sizeof(int), 1, file);
    }
    
    uint8_t r = (uint8_t)(sky_color_vec.x * 255.0f);
    uint8_t g = (uint8_t)(sky_color_vec.y * 255.0f);
    uint8_t b = (uint8_t)(sky_color_vec.z * 255.0f);
    g_sky_color_uint = (r << 16) | (g << 8) | b;

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

        if (is_scn4_format) {
            fread(new_obj->name, sizeof(char), 64, file); // Read the name, even though we don't use it in the player
        }

        fread(&new_obj->position, sizeof(vec3_t), 1, file);
        fread(&new_obj->rotation, sizeof(vec3_t), 1, file);
        fread(&new_obj->scale, sizeof(vec3_t), 1, file);

        if (is_scn4_format || is_scn3_format || is_scn2_format) {
            fread(&new_obj->material, sizeof(material_t), 1, file);
        } else { 
            vec3_t old_color;
            fread(&old_color, sizeof(vec3_t), 1, file);
            new_obj->material.diffuse_color = old_color;
            new_obj->material.specular_intensity = 0.5f;
            new_obj->material.shininess = 32.0f;
        }

        if (is_scn4_format || is_scn3_format) {
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
        if (is_scn4_format || is_scn3_format || is_scn2_format || is_scn1_format) {
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
            if (new_obj->mesh->vertex_count > 0) {
                new_obj->mesh->vertices = (vec3_t*)malloc(new_obj->mesh->vertex_count * sizeof(vec3_t));
                fread(new_obj->mesh->vertices, sizeof(vec3_t), new_obj->mesh->vertex_count, file);
            } else {
                new_obj->mesh->vertices = NULL;
            }

            fread(&new_obj->mesh->face_count, sizeof(int), 1, file);
            if (new_obj->mesh->face_count > 0) {
                new_obj->mesh->faces = (int*)malloc(new_obj->mesh->face_count * 3 * sizeof(int));
                fread(new_obj->mesh->faces, sizeof(int), new_obj->mesh->face_count * 3, file);
            } else {
                new_obj->mesh->faces = NULL;
            }
            
            mesh_calculate_normals(new_obj->mesh);
        }
        scene->objects[scene->object_count++] = new_obj;
    }

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

void setup_debug_console(void) {
    if (AllocConsole()) {
        FILE* f;
        freopen_s(&f, "CONOUT$", "w", stdout);
        SetConsoleTitle("Debug Console");
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        printf("Debug Console Initialized.\n");
    }
}
// --- Main Entry Point ---
int WINAPI WinMain(HINSTANCE instance, HINSTANCE prev_instance, LPSTR cmd_line, int cmd_show) {
	setup_debug_console(); 
    WNDCLASS window_class = {0};
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = window_callback;
    window_class.hInstance = instance;
    window_class.lpszClassName = "C_3D_Player_WindowClass";
    window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
    if (!RegisterClass(&window_class)) return 0;
    
    g_window_width = 1280;
    g_window_height = 720;
    g_render_width = 800;
    g_render_height = 600;
    
    g_window_handle = CreateWindowEx(0, window_class.lpszClassName, "My C 3D Player", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, g_window_width, g_window_height, NULL, NULL, instance, NULL);
    
    if (g_window_handle == NULL) return 0;
    
    g_framebuffer_info.bmiHeader.biSize = sizeof(g_framebuffer_info.bmiHeader);
    g_framebuffer_info.bmiHeader.biWidth = g_render_width;
    g_framebuffer_info.bmiHeader.biHeight = -g_render_height; // Top-down DIB
    g_framebuffer_info.bmiHeader.biPlanes = 1;
    g_framebuffer_info.bmiHeader.biBitCount = 32;
    g_framebuffer_info.bmiHeader.biCompression = BI_RGB;

    HDC hdc = GetDC(g_window_handle);
    HBITMAP g_framebuffer_bitmap = CreateDIBSection(
        hdc, &g_framebuffer_info, DIB_RGB_COLORS,
        &g_framebuffer_memory,
        NULL, 0
    );
    ReleaseDC(g_window_handle, hdc);
    
    g_depth_buffer = (float*)malloc(g_render_width * g_render_height * sizeof(float));

    scene_init(&g_scene);
    load_config(&g_player_config); // Load settings at startup

    OPENFILENAME ofn = {0};
    char szFile[260] = {0};

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_window_handle;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Scene Files\0*.scene\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileName(&ofn) != TRUE || !scene_load_from_file(&g_scene, ofn.lpstrFile)) {
        return 0;
    }

    for (int i = 0; i < g_scene.object_count; i++) {
        if (g_scene.objects[i]->is_player_spawn) {
            mat4_t spawn_transform = mat4_get_world_transform(&g_scene, i);
            g_player_position.x = spawn_transform.m[0][3];
            g_player_position.y = spawn_transform.m[1][3];
            g_player_position.z = spawn_transform.m[2][3];
            
            g_player_yaw = g_scene.objects[i]->rotation.z; 
            g_player_pitch = g_scene.objects[i]->rotation.x;
            
            g_player_active = 1;
        }
        if (g_scene.objects[i]->is_player_model) {
            g_player_model_index = i;
        }
    }

    if (!g_player_active) {
         MessageBox(g_window_handle, "No Player Spawn object found in the scene. The player will not be active.", "Warning", MB_OK | MB_ICONWARNING);
    } else {
        g_game_state = GAME_RUNNING;
        ShowCursor(FALSE);
        POINT screen_center = {g_window_width / 2, g_window_height / 2};
        ClientToScreen(g_window_handle, &screen_center);
        SetCursorPos(screen_center.x, screen_center.y);
    }
    
    QueryPerformanceFrequency(&g_perf_counter_freq);
    QueryPerformanceCounter(&g_last_perf_counter);

    int running = 1;
    while (running) {
        MSG message;
        while (PeekMessage(&message, NULL, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) running = 0;
            TranslateMessage(&message);
            DispatchMessage(&message);
        }

        LARGE_INTEGER current_perf_counter;
        QueryPerformanceCounter(&current_perf_counter);
        float dt = (float)(current_perf_counter.QuadPart - g_last_perf_counter.QuadPart) / (float)g_perf_counter_freq.QuadPart;
        g_last_perf_counter = current_perf_counter;
        
        if (dt > 0.1f) dt = 0.1f;

        update_player(dt);

        render_frame();

        HDC device_context = GetDC(g_window_handle);

        HDC memory_dc = CreateCompatibleDC(device_context);
        HBITMAP old_bitmap = (HBITMAP)SelectObject(memory_dc, g_framebuffer_bitmap);

        if (g_game_state == GAME_PAUSED) {
            draw_pause_menu(memory_dc);
        }

        StretchDIBits(device_context, 
                      0, 0, g_window_width, g_window_height, 
                      0, 0, g_render_width, g_render_height, 
                      g_framebuffer_memory, &g_framebuffer_info, 
                      DIB_RGB_COLORS, SRCCOPY);
        
        SelectObject(memory_dc, old_bitmap);
        DeleteDC(memory_dc);
        ReleaseDC(g_window_handle, device_context);
    }
    return 0;
}
static int ray_intersects_triangle(vec3_t ro, vec3_t rv, vec3_t v0, vec3_t v1, vec3_t v2, float* d, vec3_t* normal) {
    const float EPS = 1e-7f;
    vec3_t e1 = vec3_sub(v1, v0);
    vec3_t e2 = vec3_sub(v2, v0);
    
    vec3_t face_normal = vec3_normalize(vec3_cross(e1, e2));

    // This check is for rays parallel to the triangle plane.
    float n_dot_rv = vec3_dot(face_normal, rv);
    if (fabs(n_dot_rv) < EPS) {
        return 0; // Ray is parallel, no intersection.
    }

    vec3_t h = vec3_cross(rv, e2);
    float a = vec3_dot(e1, h);

    // This check is for rays that are nearly parallel inside the triangle plane.
    if (a > -EPS && a < EPS) return 0;

    float f = 1.f / a;
    vec3_t s = vec3_sub(ro, v0);
    float u = f * vec3_dot(s, h);
    if (u < 0.f || u > 1.f) return 0;

    vec3_t q = vec3_cross(s, e1);
    float v = f * vec3_dot(rv, q);
    if (v < 0.f || u + v > 1.f) return 0;
    
    float t = f * vec3_dot(e2, q);
    if (t > EPS) {
        if (d) *d = t;
        if (normal) {
            // If the ray and normal are pointing in the same general direction,
            // it means we've hit a back-face. We need to flip the collision normal
            // so we are always pushed "out" of the geometry.
            if (n_dot_rv > 0) {
                *normal = vec3_scale(face_normal, -1.0f);
            } else {
                *normal = face_normal;
            }
        }
        return 1;
    }
    return 0;
}
static int raycast_scene(vec3_t ray_origin, vec3_t ray_dir, float max_dist, float* hit_dist, vec3_t* hit_normal, int ignore_index) {
    int hit = 0;
    float closest_dist = max_dist;

    for (int i = 0; i < g_scene.object_count; i++) {
        if (i == ignore_index) continue; // <-- THE NEW LINE

        scene_object_t* obj = g_scene.objects[i];
        if (!obj->mesh || !obj->has_collision) continue;

        mat4_t model_matrix = mat4_get_world_transform(&g_scene, i);

        // This is a broad-phase optimization to quickly discard distant objects.
        // It's been made more robust to avoid incorrectly culling nearby floors.
        vec3_t center = { model_matrix.m[0][3], model_matrix.m[1][3], model_matrix.m[2][3] };
        float max_scale = fmax(obj->scale.x, fmax(obj->scale.y, obj->scale.z));
        float radius = 1.732f * max_scale; // Radius that encloses a 1x1x1 cube.
        
        vec3_t oc = vec3_sub(center, ray_origin);
        float b = vec3_dot(oc, ray_dir);
        float c = vec3_dot(oc, oc) - radius * radius;
        if (c > 0.0f && b < 0.0f) continue; // Sphere is behind and we're moving away
        float discriminant = b*b - c;
        if (discriminant < 0.0f) continue; // Ray misses sphere

        // Narrow-phase: Check every triangle in the mesh
        for (int j = 0; j < obj->mesh->face_count; j++) {
            int v0i = obj->mesh->faces[j * 3 + 0];
            int v1i = obj->mesh->faces[j * 3 + 1];
            int v2i = obj->mesh->faces[j * 3 + 2];

            vec4_t v0w_4 = mat4_mul_vec4(model_matrix, (vec4_t){obj->mesh->vertices[v0i].x, obj->mesh->vertices[v0i].y, obj->mesh->vertices[v0i].z, 1});
            vec4_t v1w_4 = mat4_mul_vec4(model_matrix, (vec4_t){obj->mesh->vertices[v1i].x, obj->mesh->vertices[v1i].y, obj->mesh->vertices[v1i].z, 1});
            vec4_t v2w_4 = mat4_mul_vec4(model_matrix, (vec4_t){obj->mesh->vertices[v2i].x, obj->mesh->vertices[v2i].y, obj->mesh->vertices[v2i].z, 1});
            
            vec3_t v0 = {v0w_4.x, v0w_4.y, v0w_4.z};
            vec3_t v1 = {v1w_4.x, v1w_4.y, v1w_4.z};
            vec3_t v2 = {v2w_4.x, v2w_4.y, v2w_4.z};
            
            float dist;
            vec3_t normal;
            if (ray_intersects_triangle(ray_origin, ray_dir, v0, v1, v2, &dist, &normal)) {
                if (dist < closest_dist) {
                    closest_dist = dist;
                    if(hit_normal) *hit_normal = normal;
                    hit = 1;
                }
            }
        }
    }

    if (hit) {
        if(hit_dist) *hit_dist = closest_dist;
    }
    return hit;
}
static vec3_t find_closest_point_on_line_segment(vec3_t p, vec3_t a, vec3_t b) {
    vec3_t ab = vec3_sub(b, a);
    float t = vec3_dot(vec3_sub(p, a), ab) / vec3_dot(ab, ab);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return vec3_add(a, vec3_scale(ab, t));
}
static vec3_t find_closest_point_on_triangle(vec3_t p, vec3_t a, vec3_t b, vec3_t c) {
    vec3_t ab = vec3_sub(b, a);
    vec3_t ac = vec3_sub(c, a);
    vec3_t normal = vec3_cross(ab, ac);
    float normal_len_sq = vec3_length_sq(normal);

    // Project p onto the triangle's plane
    vec3_t closest_point = (normal_len_sq > 1e-9f)
        ? vec3_sub(p, vec3_scale(normal, vec3_dot(vec3_sub(p, a), normal) / normal_len_sq))
        : a; // Fallback for degenerate (line/point) triangle

    // Check if the projected point is inside the triangle using barycentric coordinates
    float u, v, w;
    vec3_t v0 = vec3_sub(b, a), v1 = vec3_sub(c, a), v2 = vec3_sub(closest_point, a);
    float d00 = vec3_length_sq(v0), d01 = vec3_dot(v0, v1), d11 = vec3_length_sq(v1);
    float d20 = vec3_dot(v2, v0), d21 = vec3_dot(v2, v1);
    float denom = d00 * d11 - d01 * d01;

    // If triangle is degenerate, find closest point on its longest edge
    if (fabs(denom) < 1e-9f) {
        float ab_sq = vec3_length_sq(vec3_sub(b,a));
        float bc_sq = vec3_length_sq(vec3_sub(c,b));
        float ca_sq = vec3_length_sq(vec3_sub(a,c));
        if (ab_sq >= bc_sq && ab_sq >= ca_sq) return find_closest_point_on_line_segment(p, a, b);
        if (bc_sq >= ca_sq) return find_closest_point_on_line_segment(p, b, c);
        return find_closest_point_on_line_segment(p, c, a);
    }
    
    v = (d11 * d20 - d01 * d21) / denom;
    w = (d00 * d21 - d01 * d20) / denom;
    u = 1.0f - v - w;

    if (u >= 0.0f && v >= 0.0f && w >= 0.0f) {
        return closest_point; // The closest point is on the face of the triangle
    }

    // If not, the closest point is on one of the edges
    vec3_t p_ab = find_closest_point_on_line_segment(p, a, b);
    vec3_t p_bc = find_closest_point_on_line_segment(p, b, c);
    vec3_t p_ca = find_closest_point_on_line_segment(p, c, a);

    float d_ab_sq = vec3_length_sq(vec3_sub(p, p_ab));
    float d_bc_sq = vec3_length_sq(vec3_sub(p, p_bc));
    float d_ca_sq = vec3_length_sq(vec3_sub(p, p_ca));

    if (d_ab_sq <= d_bc_sq && d_ab_sq <= d_ca_sq) {
        return p_ab;
    } else if (d_bc_sq <= d_ab_sq && d_bc_sq <= d_ca_sq) {
        return p_bc;
    } else {
        return p_ca;
    }
}
static int check_sphere_world_collision(vec3_t pos, float radius, vec3_t* out_normal, float* out_depth, int ignore_index) {
    int collided = 0;
    float max_penetration = 0.0f;

    for (int i = 0; i < g_scene.object_count; i++) {
        if (i == ignore_index) continue; // <-- ADD THIS CHECK
        
        scene_object_t* obj = g_scene.objects[i];
        if (!obj->mesh || !obj->has_collision) continue;

        mat4_t model_matrix = mat4_get_world_transform(&g_scene, i);
        mat4_t inv_model_matrix = mat4_inverse(model_matrix);

        vec4_t sphere_center_local_4 = mat4_mul_vec4(inv_model_matrix, (vec4_t){pos.x, pos.y, pos.z, 1.0f});
        vec3_t sphere_center_local = {sphere_center_local_4.x, sphere_center_local_4.y, sphere_center_local_4.z};
        
        float max_scale = fmax(obj->scale.x, fmax(obj->scale.y, obj->scale.z));
        float scaled_radius = radius / max_scale;
        float scaled_radius_sq = scaled_radius * scaled_radius;

        for (int j = 0; j < obj->mesh->face_count; j++) {
            int v0i = obj->mesh->faces[j * 3 + 0];
            int v1i = obj->mesh->faces[j * 3 + 1];
            int v2i = obj->mesh->faces[j * 3 + 2];

            vec3_t v0 = obj->mesh->vertices[v0i];
            vec3_t v1 = obj->mesh->vertices[v1i];
            vec3_t v2 = obj->mesh->vertices[v2i];

            vec3_t closest_point_local = find_closest_point_on_triangle(sphere_center_local, v0, v1, v2);

            vec3_t delta = vec3_sub(sphere_center_local, closest_point_local);
            float dist_sq = vec3_dot(delta, delta);

            if (dist_sq < scaled_radius_sq) {
                float dist = sqrtf(dist_sq);
                float penetration = scaled_radius - dist;

                if (penetration > max_penetration) {
                    collided = 1;
                    max_penetration = penetration;

                    vec3_t normal_local = (dist > 1e-6) ? vec3_scale(delta, 1.0f / dist) : vec3_normalize(vec3_cross(vec3_sub(v1,v0), vec3_sub(v2,v0)));
                    
                    vec4_t normal_world_4 = mat4_mul_vec4(model_matrix, (vec4_t){normal_local.x, normal_local.y, normal_local.z, 0.0f});
                    *out_normal = vec3_normalize((vec3_t){normal_world_4.x, normal_world_4.y, normal_world_4.z});
                    
                    *out_depth = penetration * max_scale;
                }
            }
        }
    }
    return collided;
}
static vec3_t collide_and_slide(vec3_t pos, vec3_t vel, float radius, int ignore_index, int* out_on_ground) {
    const int MAX_SLIDES = 4;
    const float BOUNCE_FACTOR = 0.1f; 

    // Initialize to not on ground at the start of the movement
    *out_on_ground = 0;

    for (int slide = 0; slide < MAX_SLIDES; slide++) {
        vec3_t destination = vec3_add(pos, vel);

        vec3_t collision_normal;
        float penetration_depth;
        if (!check_sphere_world_collision(destination, radius, &collision_normal, &penetration_depth, ignore_index)) {
            // No collision on this path, return the final destination
            return destination; 
        }

        // A collision occurred, adjust position to be just outside the surface
        pos = vec3_add(pos, vec3_scale(collision_normal, penetration_depth));
        
        // If the collision was with a walkable surface, set the ground flag.
        // We check if the normal is pointing mostly upwards.
        if (collision_normal.z > 0.7f) {
            *out_on_ground = 1;
        }

        // Project the velocity onto the collision plane to get the "slide" vector
        float dot_product = vec3_dot(vel, collision_normal);
        vec3_t slide_vel = vec3_sub(vel, vec3_scale(collision_normal, dot_product));
        
        // Add a small bounce to prevent getting stuck in crevices
        vec3_t bounce_vel = vec3_scale(collision_normal, -dot_product * BOUNCE_FACTOR);

        // The new velocity for the next iteration is the slide plus the bounce
        vel = vec3_add(slide_vel, bounce_vel);
    }

    // If we finished all slide iterations, it means we're likely stuck. Return the last safe position.
    return pos;
}
void update_player(float dt) {
    if (!g_player_active || g_game_state == GAME_PAUSED) return;

    // --- MOUSE LOOK AND TAB LOGIC (Unchanged) ---
    int is_tab_down = (GetKeyState(VK_TAB) & 0x8000) != 0;
    if (is_tab_down != g_mouse_is_free) {
        g_mouse_is_free = is_tab_down;
        ShowCursor(g_mouse_is_free);
        if (!g_mouse_is_free) {
            POINT screen_center = {g_window_width / 2, g_window_height / 2};
            ClientToScreen(g_window_handle, &screen_center);
            SetCursorPos(screen_center.x, screen_center.y);
        }
    }

    if (!g_mouse_is_free) {
        POINT p;
        if (GetCursorPos(&p) && ScreenToClient(g_window_handle, &p)) {
            int center_x = g_window_width / 2;
            int center_y = g_window_height / 2;
            float dx = (float)(p.x - center_x);
            float dy = (float)(p.y - center_y);

            if (dx != 0 || dy != 0) {
                g_player_yaw -= dx * g_player_config.mouse_sensitivity;
                g_player_pitch -= dy * g_player_config.mouse_sensitivity;

                if (g_player_pitch > 1.5f) g_player_pitch = 1.5f;
                if (g_player_pitch < -1.5f) g_player_pitch = -1.5f;

                POINT screen_center = {center_x, center_y};
                ClientToScreen(g_window_handle, &screen_center);
                SetCursorPos(screen_center.x, screen_center.y);
            }
        }
    }
    
    // --- 1. CALCULATE MOVEMENT INTENT (WISH DIRECTION) ---
    vec3_t wish_dir = {0};
    if (GetKeyState('W') & 0x8000) wish_dir.x += 1.0f;
    if (GetKeyState('S') & 0x8000) wish_dir.x -= 1.0f;
    if (GetKeyState('A') & 0x8000) wish_dir.y -= 1.0f;
    if (GetKeyState('D') & 0x8000) wish_dir.y += 1.0f;

    vec3_t forward_dir = {cosf(g_player_yaw), sinf(g_player_yaw), 0};
    vec3_t right_dir   = {sinf(g_player_yaw), -cosf(g_player_yaw), 0};
    wish_dir = vec3_normalize(vec3_add(vec3_scale(forward_dir, wish_dir.x), vec3_scale(right_dir, wish_dir.y)));
    
    // --- 2. UPDATE VELOCITY BASED ON STATE (GROUNDED VS AIR) ---
    vec3_t horizontal_vel = { g_player_velocity.x, g_player_velocity.y, 0 };
    
    if (g_player_on_ground) {
        // --- Ground Movement ---
        float current_speed = vec3_dot(horizontal_vel, wish_dir);
        float add_speed = PLAYER_SPEED - current_speed;
        if (add_speed > 0) {
            float accel_speed = PLAYER_ACCELERATION * dt;
            if (accel_speed > add_speed) {
                accel_speed = add_speed;
            }
            g_player_velocity.x += wish_dir.x * accel_speed;
            g_player_velocity.y += wish_dir.y * accel_speed;
        }

        // Apply friction
        float speed = vec3_length(horizontal_vel);
        if (speed > 0) {
            float friction_drop = speed * PLAYER_FRICTION * dt;
            float new_speed = speed - friction_drop;
            if (new_speed < 0) new_speed = 0;
            if (speed > 0) {
                g_player_velocity.x *= (new_speed / speed);
                g_player_velocity.y *= (new_speed / speed);
            }
        }

        // Handle jumping
        if ((GetKeyState(VK_SPACE) & 0x8000)) {
            g_player_velocity.z = PLAYER_JUMP_FORCE;
            g_player_on_ground = 0;
        }
    } else {
        // --- Air Movement ---
        float current_speed = vec3_dot(horizontal_vel, wish_dir);
        float add_speed = PLAYER_SPEED - current_speed;
        if (add_speed > 0) {
            float accel_speed = PLAYER_AIR_ACCELERATION * dt;
            if (accel_speed > add_speed) {
                accel_speed = add_speed;
            }
            g_player_velocity.x += wish_dir.x * accel_speed;
            g_player_velocity.y += wish_dir.y * accel_speed;
        }
    }

    // --- 3. APPLY GRAVITY (ONLY WHEN IN THE AIR) ---
    if (!g_player_on_ground) {
        g_player_velocity.z += PLAYER_GRAVITY * dt;
    }

    // --- 4. COLLIDE AND SLIDE FOR THE ENTIRE CAPSULE ---
    vec3_t move_delta = vec3_scale(g_player_velocity, dt);

    vec3_t bottom_sphere_pos = g_player_position;
    bottom_sphere_pos.z += PLAYER_RADIUS;
    
    vec3_t top_sphere_pos = g_player_position;
    top_sphere_pos.z += PLAYER_HEIGHT - PLAYER_RADIUS;

    int grounded_this_frame = 0;
    int bottom_is_grounded = 0;
    int top_is_grounded = 0;

    vec3_t resolved_bottom_pos = collide_and_slide(bottom_sphere_pos, move_delta, PLAYER_RADIUS, g_player_model_index, &bottom_is_grounded);
    vec3_t resolved_top_pos = collide_and_slide(top_sphere_pos, move_delta, PLAYER_RADIUS, g_player_model_index, &top_is_grounded);
    
    grounded_this_frame = bottom_is_grounded || top_is_grounded;

    vec3_t final_pos;
    vec3_t final_displacement_bottom = vec3_sub(resolved_bottom_pos, bottom_sphere_pos);
    vec3_t final_displacement_top = vec3_sub(resolved_top_pos, top_sphere_pos);
    
    if (vec3_length_sq(final_displacement_bottom) > vec3_length_sq(final_displacement_top)) {
         final_pos = vec3_sub(resolved_bottom_pos, (vec3_t){0, 0, PLAYER_RADIUS});
    } else {
         final_pos = vec3_sub(resolved_top_pos, (vec3_t){0, 0, PLAYER_HEIGHT - PLAYER_RADIUS});
    }


    // --- 5. UPDATE FINAL POSITION AND VELOCITY ---
    // If there was no significant movement, we can avoid calculating velocity
    // from position, which can introduce floating point errors.
    if (vec3_length_sq(move_delta) > 1e-12) {
        vec3_t actual_velocity = vec3_scale(vec3_sub(final_pos, g_player_position), 1.0f / dt);
        g_player_velocity = actual_velocity;
    } else {
        g_player_velocity = (vec3_t){0,0,0};
    }
    g_player_position = final_pos;

    // --- 6. UPDATE GROUNDED STATE ---
    g_player_on_ground = grounded_this_frame;
    if (g_player_on_ground && g_player_velocity.z < 0) {
        g_player_velocity.z = 0; // Stop downward velocity when we land
    }
}
void render_frame() {
    if (!g_framebuffer_memory) return;
    
    uint32_t* pixel = (uint32_t*)g_framebuffer_memory;
    for (int i = 0; i < g_render_width * g_render_height; ++i) {
        *pixel++ = g_sky_color_uint;
        g_depth_buffer[i] = FLT_MAX;
    }

    vec3_t camera_pos;
    mat4_t view_matrix;

    if (g_player_active) {
        if (g_player_model_index != -1) {
            scene_object_t* player_model = g_scene.objects[g_player_model_index];

            player_model->position = g_player_position;
            player_model->rotation.z = g_player_yaw; 

            mat4_t player_transform = mat4_get_world_transform(&g_scene, g_player_model_index);
            vec4_t target_local = {player_model->camera_offset.x, player_model->camera_offset.y, player_model->camera_offset.z, 1.0f};
            vec4_t target_world_4 = mat4_mul_vec4(player_transform, target_local);
            vec3_t camera_target = {target_world_4.x, target_world_4.y, target_world_4.z};

            vec3_t offset;
            offset.x = g_camera_distance * cosf(g_player_pitch) * cosf(g_player_yaw);
            offset.y = g_camera_distance * cosf(g_player_pitch) * sinf(g_player_yaw);
            offset.z = g_camera_distance * sinf(g_player_pitch);
            
            camera_pos = vec3_add(camera_target, vec3_scale(offset, -1.0f));
            
            vec3_t up_vector = {0, 0, 1};
            if (fabs(sinf(g_player_pitch)) > 0.999f) {
                up_vector = (vec3_t){0, 1, 0};
            }
            view_matrix = mat4_look_at(camera_pos, camera_target, up_vector);

        } else { 
            vec3_t eye_pos = g_player_position;
            eye_pos.z += PLAYER_EYE_HEIGHT;
            camera_pos = eye_pos;

            vec3_t look_direction;
            look_direction.x = cosf(g_player_pitch) * cosf(g_player_yaw);
            look_direction.y = cosf(g_player_pitch) * sinf(g_player_yaw);
            look_direction.z = sinf(g_player_pitch);
            
            vec3_t camera_target = vec3_add(eye_pos, look_direction);
            
            vec3_t up_vector = {0, 0, 1};
            if (fabs(look_direction.z) > 0.999f) {
                up_vector = (vec3_t){0, 1, 0};
            }
            
            view_matrix = mat4_look_at(eye_pos, camera_target, up_vector);
        }

    } else { 
        vec3_t offset;
        offset.x = g_camera_distance * cosf(g_camera_pitch) * cosf(g_camera_yaw);
        offset.y = g_camera_distance * cosf(g_camera_pitch) * sinf(g_camera_yaw);
        offset.z = g_camera_distance * sinf(g_camera_pitch);
        camera_pos = vec3_add(g_camera_target, offset);

        vec3_t up_vector = {0, 0, 1};
        if (fabs(sinf(g_camera_pitch)) > 0.999f) {
            up_vector = (vec3_t){0, 1, 0}; 
        }
        view_matrix = mat4_look_at(camera_pos, g_camera_target, up_vector);
    }
    
    float fov_radians = g_player_config.fov_degrees * (3.14159f / 180.0f);
    mat4_t projection_matrix = mat4_perspective(fov_radians, (float)g_render_width / (float)g_render_height, 0.1f, 100.0f);
    
    for (int i = 0; i < g_scene.object_count; i++) {
        if (g_scene.objects[i]->is_player_spawn) {
            continue;
        }
        if (i == g_player_model_index && g_camera_distance < 1.0f) {
             continue; // Don't render player model if camera is too close
        }
        render_object(g_scene.objects[i], i, view_matrix, projection_matrix, camera_pos);
    }
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
            out_tri1->vertices[i + 1].w = inside_points[0].w + t * (outside_points[i].w - inside_points[0].w);

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
void draw_gouraud_triangle(vec4_t p0, vec4_t p1, vec4_t p2, vec3_t c0, vec3_t c1, vec3_t c2) {
    if (p0.w <= 0 || p1.w <= 0 || p2.w <= 0) return;

    float p0w_inv = 1.0f / p0.w; p0.x *= p0w_inv; p0.y *= p0w_inv; p0.z *= p0w_inv;
    float p1w_inv = 1.0f / p1.w; p1.x *= p1w_inv; p1.y *= p1w_inv; p1.z *= p1w_inv;
    float p2w_inv = 1.0f / p2.w; p2.x *= p2w_inv; p2.y *= p2w_inv; p2.z *= p2w_inv;

    p0.x = (p0.x + 1.0f) * 0.5f * g_render_width; p0.y = (1.0f - p0.y) * 0.5f * g_render_height; // MODIFIED
    p1.x = (p1.x + 1.0f) * 0.5f * g_render_width; p1.y = (1.0f - p1.y) * 0.5f * g_render_height; // MODIFIED
    p2.x = (p2.x + 1.0f) * 0.5f * g_render_width; p2.y = (1.0f - p2.y) * 0.5f * g_render_height; // MODIFIED

    vec3_t c0_pw = vec3_scale(c0, p0w_inv);
    vec3_t c1_pw = vec3_scale(c1, p1w_inv);
    vec3_t c2_pw = vec3_scale(c2, p2w_inv);

    if (p0.y > p1.y) { vec4_t tp = p0; p0 = p1; p1 = tp; vec3_t tc = c0_pw; c0_pw = c1_pw; c1_pw = tc; float tw = p0w_inv; p0w_inv = p1w_inv; p1w_inv = tw; }
    if (p0.y > p2.y) { vec4_t tp = p0; p0 = p2; p2 = tp; vec3_t tc = c0_pw; c0_pw = c2_pw; c2_pw = tc; float tw = p0w_inv; p0w_inv = p2w_inv; p2w_inv = tw; }
    if (p1.y > p2.y) { vec4_t tp = p1; p1 = p2; p2 = tp; vec3_t tc = c1_pw; c1_pw = c2_pw; c2_pw = tc; float tw = p1w_inv; p1w_inv = p2w_inv; p2w_inv = tw; }

    int y_start = (int)(p0.y + 0.5f);
    int y_end = (int)(p2.y + 0.5f);

    y_start = (y_start < 0) ? 0 : y_start;
    y_end = (y_end > g_render_height) ? g_render_height : y_end; // MODIFIED

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
        x_end = (x_end > g_render_width) ? g_render_width : x_end; // MODIFIED

        float scanline_width = xb_f - xa_f;
        if (scanline_width <= 0) continue;

        vec3_t c_pw_step = vec3_scale(vec3_sub(cb_pw, ca_pw), 1.0f / scanline_width);
        float w_inv_step = (wb_inv - wa_inv) / scanline_width;

        float initial_offset = (float)x_start - xa_f;
        vec3_t current_c_pw = vec3_add(ca_pw, vec3_scale(c_pw_step, initial_offset));
        float current_w_inv = wa_inv + w_inv_step * initial_offset;
        
        uint32_t* row = (uint32_t*)g_framebuffer_memory + y * g_render_width; // MODIFIED
        float* depth_row = g_depth_buffer + y * g_render_width; // MODIFIED

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
void render_object(scene_object_t* object, int object_index, mat4_t view_matrix, mat4_t projection_matrix, vec3_t camera_pos) {
    if (object->light_properties || !object->mesh || !object->mesh->normals) {
        return;
    }
    
    // --- NEW: Resize global buffers if necessary ---
    if (object->mesh->vertex_count > g_vertex_buffer_capacity) {
        g_vertex_buffer_capacity = object->mesh->vertex_count;
        g_clip_coords_buffer = (vec4_t*)realloc(g_clip_coords_buffer, g_vertex_buffer_capacity * sizeof(vec4_t));
        g_colors_buffer = (vec3_t*)realloc(g_colors_buffer, g_vertex_buffer_capacity * sizeof(vec3_t));
        if (!g_clip_coords_buffer || !g_colors_buffer) {
             g_vertex_buffer_capacity = 0; // Reset on failure
             return;
        }
    }

    mat4_t model_matrix = mat4_get_world_transform(&g_scene, object_index);
    mat4_t final_transform = mat4_mul_mat4(projection_matrix, mat4_mul_mat4(view_matrix, model_matrix));

    for (int i = 0; i < object->mesh->vertex_count; i++) {
        // Transform vertex position to clip space
        g_clip_coords_buffer[i] = mat4_mul_vec4(final_transform, (vec4_t){
            object->mesh->vertices[i].x, 
            object->mesh->vertices[i].y, 
            object->mesh->vertices[i].z, 
            1.0f
        });

        // --- Per-Vertex Lighting Calculation ---
        vec4_t v_world_4 = mat4_mul_vec4(model_matrix, (vec4_t){object->mesh->vertices[i].x, object->mesh->vertices[i].y, object->mesh->vertices[i].z, 1.0f});
        vec3_t v_world = {v_world_4.x, v_world_4.y, v_world_4.z};
        
        vec4_t n_world_4 = mat4_mul_vec4(model_matrix, (vec4_t){object->mesh->normals[i].x, object->mesh->normals[i].y, object->mesh->normals[i].z, 0.0f});
        vec3_t n_world = vec3_normalize((vec3_t){n_world_4.x, n_world_4.y, n_world_4.z});
        
        vec3_t diffuse_sum = {0.1f, 0.1f, 0.1f}; // Ambient term
        vec3_t specular_sum = {0,0,0};
        vec3_t view_dir = vec3_normalize(vec3_sub(camera_pos, v_world));

        for (int l = 0; l < g_scene.object_count; l++) {
            scene_object_t* light_obj = g_scene.objects[l];
            if (!light_obj->light_properties) continue;

            mat4_t light_transform = mat4_get_world_transform(&g_scene, l);
            vec3_t light_pos = { light_transform.m[0][3], light_transform.m[1][3], light_transform.m[2][3] };
            vec3_t to_light = vec3_sub(light_pos, v_world);
            float dist_sq = vec3_dot(to_light, to_light);
            if(dist_sq < 1e-6) dist_sq = 1e-6;
            vec3_t light_dir = vec3_normalize(to_light);
            float attenuation = light_obj->light_properties->intensity / dist_sq;
            
            float diff_intensity = fmax(vec3_dot(n_world, light_dir), 0.0f);
            
            if (light_obj->light_properties->type == LIGHT_TYPE_SPOT) {
                mat4_t rot_matrix = mat4_mul_mat4(mat4_rotation_z(light_obj->rotation.z), mat4_mul_mat4(mat4_rotation_y(light_obj->rotation.y), mat4_rotation_x(light_obj->rotation.x)));
                vec4_t local_dir = {0, 0, -1, 0}; 
                vec4_t world_dir4 = mat4_mul_vec4(rot_matrix, local_dir);
                vec3_t spot_dir = vec3_normalize((vec3_t){world_dir4.x, world_dir4.y, world_dir4.z});
                
                float theta = vec3_dot(light_dir, vec3_scale(spot_dir, -1.0f));
                float epsilon = cosf(light_obj->light_properties->spot_angle / 2.0f);
                if (theta > epsilon) {
                     float falloff_angle = (light_obj->light_properties->spot_angle / 2.0f) * (1.0f - light_obj->light_properties->spot_blend);
                     float falloff_cos = cosf(falloff_angle);
                     float spot_effect = (theta - epsilon) / (falloff_cos - epsilon);
                     spot_effect = (spot_effect < 0.0f) ? 0.0f : (spot_effect > 1.0f) ? 1.0f : spot_effect;
                     attenuation *= spot_effect;
                } else {
                    attenuation = 0;
                }
            }

            if (attenuation > 0) {
                diffuse_sum = vec3_add(diffuse_sum, vec3_scale(light_obj->light_properties->color, diff_intensity * attenuation));
                
                if(diff_intensity > 0.0f && object->material.specular_intensity > 0.0f) {
                    vec3_t reflect_dir = vec3_sub(vec3_scale(n_world, 2.0f * vec3_dot(n_world, light_dir)), light_dir);
                    float spec_angle = fmax(vec3_dot(view_dir, reflect_dir), 0.0f);
                    float specular_term = powf(spec_angle, object->material.shininess);
                    specular_sum = vec3_add(specular_sum, vec3_scale(light_obj->light_properties->color, specular_term * object->material.specular_intensity * attenuation));
                }
            }
        }
        g_colors_buffer[i].x = object->material.diffuse_color.x * diffuse_sum.x + specular_sum.x;
        g_colors_buffer[i].y = object->material.diffuse_color.y * diffuse_sum.y + specular_sum.y;
        g_colors_buffer[i].z = object->material.diffuse_color.z * diffuse_sum.z + specular_sum.z;
    }

    // --- Render faces using pre-calculated data ---
    for (int i = 0; i < object->mesh->face_count; ++i) {
        int v_indices[3] = {object->mesh->faces[i*3+0], object->mesh->faces[i*3+1], object->mesh->faces[i*3+2]};
        
        // --- Backface Culling ---
        vec4_t v0_clip = g_clip_coords_buffer[v_indices[0]];
        vec4_t v1_clip = g_clip_coords_buffer[v_indices[1]];
        vec4_t v2_clip = g_clip_coords_buffer[v_indices[2]];
        
        if (v0_clip.w > 0 && v1_clip.w > 0 && v2_clip.w > 0) {
             vec3_t v0_ndc = {v0_clip.x/v0_clip.w, v0_clip.y/v0_clip.w, v0_clip.z/v0_clip.w};
             vec3_t v1_ndc = {v1_clip.x/v1_clip.w, v1_clip.y/v1_clip.w, v1_clip.z/v1_clip.w};
             vec3_t v2_ndc = {v2_clip.x/v2_clip.w, v2_clip.y/v2_clip.w, v2_clip.z/v2_clip.w};
             float signed_area_z = (v1_ndc.x - v0_ndc.x) * (v2_ndc.y - v0_ndc.y) - (v1_ndc.y - v0_ndc.y) * (v2_ndc.x - v0_ndc.x);
             if (!object->is_double_sided && signed_area_z < 0) {
                 continue;
             }
        }
        
        // --- Assemble triangle for clipping ---
        triangle_t original_tri;
        original_tri.vertices[0] = v0_clip;
        original_tri.vertices[1] = v1_clip;
        original_tri.vertices[2] = v2_clip;
        original_tri.colors[0] = g_colors_buffer[v_indices[0]];
        original_tri.colors[1] = g_colors_buffer[v_indices[1]];
        original_tri.colors[2] = g_colors_buffer[v_indices[2]];

        // --- Clip and Draw ---
        triangle_t clipped_tris[2];
        int num_clipped = clip_triangle_against_near_plane(&original_tri, &clipped_tris[0], &clipped_tris[1]);

        for (int t = 0; t < num_clipped; t++) {
            draw_gouraud_triangle(
                clipped_tris[t].vertices[0], clipped_tris[t].vertices[1], clipped_tris[t].vertices[2],
                clipped_tris[t].colors[0], clipped_tris[t].colors[1], clipped_tris[t].colors[2]
            );
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
        // Lines parallel to the Y-axis
        vec4_t p1 = {i, -half_size, 0, 1}, p2 = {i, half_size, 0, 1};
        // Lines parallel to the X-axis
        vec4_t p3 = {-half_size, i, 0, 1}, p4 = {half_size, i, 0, 1};
        
        vec4_t sp[4] = {mat4_mul_vec4(vp_matrix,p1), mat4_mul_vec4(vp_matrix,p2), mat4_mul_vec4(vp_matrix,p3), mat4_mul_vec4(vp_matrix,p4)};
        for(int j=0;j<4;++j){if(sp[j].w!=0){sp[j].x=(sp[j].x/sp[j].w+1)*0.5f*g_render_width; sp[j].y=(1-sp[j].y/sp[j].w)*0.5f*g_render_height; sp[j].z=sp[j].z/sp[j].w;}}
        draw_line(sp[0].x,sp[0].y,sp[0].z, sp[1].x,sp[1].y,sp[1].z, grid_color);
        draw_line(sp[2].x,sp[2].y,sp[2].z, sp[3].x,sp[3].y,sp[3].z, grid_color);
    }
    
    // Draw the main axes
    vec4_t x1={-half_size,0,0,1}, x2={half_size,0,0,1}; // X-axis
    vec4_t y1={0,-half_size,0,1}, y2={0,half_size,0,1}; // Y-axis
    vec4_t z1={0,0,-half_size,1}, z2={0,0,half_size,1}; // Z-axis

    vec4_t axis_p[6] = {mat4_mul_vec4(vp_matrix,x1), mat4_mul_vec4(vp_matrix,x2), mat4_mul_vec4(vp_matrix,y1), mat4_mul_vec4(vp_matrix,y2), mat4_mul_vec4(vp_matrix,z1), mat4_mul_vec4(vp_matrix,z2)};
    // --- FIX: The typo is on this line ---
    for(int j=0;j<6;++j){if(axis_p[j].w!=0){axis_p[j].x=(axis_p[j].x/axis_p[j].w+1)*0.5f*g_render_width; axis_p[j].y=(1-axis_p[j].y/axis_p[j].w)*0.5f*g_render_height; axis_p[j].z=axis_p[j].z/axis_p[j].w;}} // Changed sp[j].w to axis_p[j].w
    
    draw_line(axis_p[0].x,axis_p[0].y,axis_p[0].z, axis_p[1].x,axis_p[1].y,axis_p[1].z, axis_color_x); // Red X
    draw_line(axis_p[2].x,axis_p[2].y,axis_p[2].z, axis_p[3].x,axis_p[3].y,axis_p[3].z, axis_color_y); // Green Y
    draw_line(axis_p[4].x,axis_p[4].y,axis_p[4].z, axis_p[5].x,axis_p[5].y,axis_p[5].z, axis_color_z); // Blue Z
}
// --- Drawing Helper Functions ---
void draw_pixel(int x, int y, float z, uint32_t c) {
    if (x >= 0 && x < g_render_width && y >= 0 && y < g_render_height) { // MODIFIED
        int i = x + y * g_render_width; // MODIFIED
        if (z < g_depth_buffer[i]) {
            *((uint32_t*)g_framebuffer_memory + i) = c;
            g_depth_buffer[i] = z;
        }
    }
}
void draw_line(int x0,int y0,float z0,int x1,int y1,float z1,uint32_t c){int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy,e2;float z=z0,dz=(z1-z0)/sqrtf((float)(x1-x0)*(x1-x0)+(y1-y0)*(y1-y0));for(;;){draw_pixel(x0,y0,z,c);if(x0==x1&&y0==y1)break;e2=2*err;if(e2>=dy){err+=dy;x0+=sx;z+=dz*sx;}if(e2<=dx){err+=dx;y0+=sy;z+=dz*sy;}}}

LRESULT CALLBACK window_callback(HWND window_handle, UINT message, WPARAM w_param, LPARAM l_param) {
    LRESULT result = 0;
    switch (message) {
        case WM_CLOSE: case WM_DESTROY: {
            scene_destroy(&g_scene);
            if (g_clip_coords_buffer) free(g_clip_coords_buffer);
            if (g_colors_buffer) free(g_colors_buffer);
            if (g_depth_buffer) free(g_depth_buffer);
            PostQuitMessage(0);
        } break;
        case WM_SIZE: {
            g_window_width = LOWORD(l_param); 
            g_window_height = HIWORD(l_param);
        } break;
        case WM_LBUTTONDOWN: {
            if (g_game_state == GAME_PAUSED) {
                int mouse_x = LOWORD(l_param);
                int mouse_y = HIWORD(l_param);

                float scaled_mx = (float)mouse_x * ((float)g_render_width / (float)g_window_width);
                float scaled_my = (float)mouse_y * ((float)g_render_height / (float)g_window_height);
                POINT pt = { (int)scaled_mx, (int)scaled_my };

                if (PtInRect(&g_resume_button_rect, pt)) {
                    g_game_state = GAME_RUNNING;
                    ShowCursor(FALSE);
                    g_mouse_is_free = 0;
                    POINT screen_center = {g_window_width / 2, g_window_height / 2};
                    ClientToScreen(g_window_handle, &screen_center);
                    SetCursorPos(screen_center.x, screen_center.y);
                } else if (PtInRect(&g_exit_button_rect, pt)) {
                    PostQuitMessage(0);
                } else if (PtInRect(&g_fov_minus_rect, pt)) {
                    g_player_config.fov_degrees -= 5.0f;
                    if (g_player_config.fov_degrees < 40.0f) g_player_config.fov_degrees = 40.0f;
                    save_config(&g_player_config);
                } else if (PtInRect(&g_fov_plus_rect, pt)) {
                    g_player_config.fov_degrees += 5.0f;
                    if (g_player_config.fov_degrees > 120.0f) g_player_config.fov_degrees = 120.0f;
                    save_config(&g_player_config);
                } else if (PtInRect(&g_sens_minus_rect, pt)) {
                    g_player_config.mouse_sensitivity -= 0.0005f;
                    if (g_player_config.mouse_sensitivity < 0.0005f) g_player_config.mouse_sensitivity = 0.0005f;
                    save_config(&g_player_config);
                } else if (PtInRect(&g_sens_plus_rect, pt)) {
                    g_player_config.mouse_sensitivity += 0.0005f;
                    save_config(&g_player_config);
                }
            }
        } break;
        case WM_MBUTTONDOWN: {
            g_middle_mouse_down = 1;
            g_last_mouse_x = LOWORD(l_param);
            g_last_mouse_y = HIWORD(l_param);
            SetCapture(window_handle);
        } break;
        case WM_MBUTTONUP: {
            g_middle_mouse_down = 0;
            ReleaseCapture();
        } break;
        case WM_MOUSEWHEEL: {
            if (g_game_state == GAME_RUNNING) {
                int delta = GET_WHEEL_DELTA_WPARAM(w_param);
                if (delta > 0) g_camera_distance -= 0.5f; else g_camera_distance += 0.5f;
                if (g_camera_distance < 0.5f) g_camera_distance = 0.5f; // Allow getting closer
                if (g_camera_distance > 40.0f) g_camera_distance = 40.0f;
            }
        } break;
        case WM_MOUSEMOVE: {
            int cur_x = LOWORD(l_param), cur_y = HIWORD(l_param);
            int dx = cur_x - g_last_mouse_x, dy = cur_y - g_last_mouse_y;
            if (!g_player_active && g_middle_mouse_down) {
                 if (GetKeyState(VK_SHIFT) & 0x8000) {
                    float sens = 0.0025f * g_camera_distance;
                    vec3_t offset;
                    offset.x = g_camera_distance * cosf(g_camera_pitch) * cosf(g_camera_yaw);
                    offset.y = g_camera_distance * cosf(g_camera_pitch) * sinf(g_camera_yaw);
                    offset.z = g_camera_distance * sinf(g_camera_pitch);
                    vec3_t cam_pos = vec3_add(g_camera_target, offset); 
                    vec3_t fwd = vec3_normalize(vec3_sub(g_camera_target, cam_pos)); 
                    vec3_t right = vec3_normalize(vec3_cross(fwd, (vec3_t){0,0,1})); 
                    vec3_t up = vec3_normalize(vec3_cross(right, fwd));
                    g_camera_target = vec3_add(g_camera_target, vec3_add(vec3_scale(right, -dx * sens), vec3_scale(up, dy * sens)));
                } else {
                    g_camera_yaw += (float)dx * 0.01f;
                    g_camera_pitch += (float)dy * 0.01f;
                    if (g_camera_pitch > 1.5f) g_camera_pitch = 1.5f;
                    if (g_camera_pitch < -1.5f) g_camera_pitch = -1.5f;
                }
            }
            g_last_mouse_x = cur_x;
            g_last_mouse_y = cur_y;
        } break;
        case WM_KEYDOWN: {
            if (w_param == VK_ESCAPE) {
                if (g_game_state == GAME_RUNNING) {
                    g_game_state = GAME_PAUSED;
                    g_mouse_is_free = 1;
                    ShowCursor(TRUE);
                } else {
                    g_game_state = GAME_RUNNING;
                    g_mouse_is_free = 0;
                    ShowCursor(FALSE);
                    POINT screen_center = {g_window_width / 2, g_window_height / 2};
                    ClientToScreen(g_window_handle, &screen_center);
                    SetCursorPos(screen_center.x, screen_center.y);
                }
            }
        } break;
        default: {
            result = DefWindowProc(window_handle, message, w_param, l_param);
        }
    }
    return result;
}
