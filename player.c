//gcc player.c math3d.c -o player.exe -lgdi32 -luser32 -lcomdlg32 -lmsimg32

#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <float.h>
#include <stdio.h>
#include "math3d.h"
#include <math.h>

// --- Global Variables ---
static HWND g_window_handle;
static BITMAPINFO g_framebuffer_info;
static void* g_framebuffer_memory;
static float* g_depth_buffer;
static int g_framebuffer_width;
static int g_framebuffer_height;
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

// --- Function Declarations ---
LRESULT CALLBACK window_callback(HWND, UINT, WPARAM, LPARAM);
void render_frame();
void draw_pixel(int, int, float, uint32_t);
void draw_line(int x0, int y0, float z0, int x1, int y1, float z1, uint32_t color);
void draw_filled_triangle(vec4_t, vec4_t, vec4_t, uint32_t);
void render_object(scene_object_t* object, int object_index, mat4_t view_matrix, mat4_t projection_matrix, vec3_t camera_pos);
void render_grid(mat4_t view_matrix, mat4_t projection_matrix);

// Scene I/O
void scene_init(scene_t* scene);
void scene_destroy(scene_t* scene);
int scene_load_from_file(scene_t* scene, const char* filename);
void destroy_mesh_data(mesh_t* mesh);

// --- Scene Management ---
void scene_init(scene_t* scene) {
    scene->capacity = 10;
    scene->object_count = 0;
    scene->objects = (scene_object_t**)malloc(scene->capacity * sizeof(scene_object_t*));
}

void destroy_mesh_data(mesh_t* mesh) {
    if (!mesh) return;
    if (mesh->vertices) free(mesh->vertices);
    if (mesh->faces) free(mesh->faces);
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
int scene_load_from_file(scene_t* scene, const char* filename) {
    if (!scene || !filename) return 0;

    FILE* file = fopen(filename, "rb");
    if (!file) {
        return 0;
    }

    char header[4];
    fread(header, sizeof(char), 4, file);

    int is_scn2_format = (strncmp(header, "SCN2", 4) == 0);
    int is_scn1_format = (strncmp(header, "SCN1", 4) == 0);
    
    vec3_t sky_color_vec;
    int object_count = 0;

    if (is_scn2_format) {
        fread(&sky_color_vec, sizeof(vec3_t), 1, file);
        fread(&object_count, sizeof(int), 1, file);
    } else if (is_scn1_format) {
        fread(&sky_color_vec, sizeof(vec3_t), 1, file);
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

        fread(&new_obj->position, sizeof(vec3_t), 1, file);
        fread(&new_obj->rotation, sizeof(vec3_t), 1, file);
        fread(&new_obj->scale, sizeof(vec3_t), 1, file);

        if (is_scn2_format) {
            fread(&new_obj->material, sizeof(material_t), 1, file);
        } else {
            vec3_t old_color;
            fread(&old_color, sizeof(vec3_t), 1, file);
            new_obj->material.diffuse_color = old_color;
            new_obj->material.specular_intensity = 0.5f;
            new_obj->material.shininess = 32.0f;
        }

        if (is_scn1_format || is_scn2_format) {
            fread(&new_obj->parent_index, sizeof(int), 1, file);
            fread(&new_obj->is_double_sided, sizeof(int), 1, file);
            fread(&new_obj->is_static, sizeof(int), 1, file);
        } else {
            new_obj->parent_index = -1;
            new_obj->is_double_sided = 0;
            new_obj->is_static = 0;
        }

        int is_light = 0;
        if (is_scn1_format || is_scn2_format) {
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
// --- Main Entry Point ---
int WINAPI WinMain(HINSTANCE instance, HINSTANCE prev_instance, LPSTR cmd_line, int cmd_show) {
    WNDCLASS window_class = {0};
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = window_callback;
    window_class.hInstance = instance;
    window_class.lpszClassName = "C_3D_Player_WindowClass";
    window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
    if (!RegisterClass(&window_class)) return 0;
    
    g_framebuffer_width = 800;
    g_framebuffer_height = 600;
    
    g_window_handle = CreateWindowEx(0, window_class.lpszClassName, "My C 3D Player", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, g_framebuffer_width, g_framebuffer_height, NULL, NULL, instance, NULL);
    
    if (g_window_handle == NULL) return 0;

    scene_init(&g_scene);

    // --- Player-specific logic: Force user to load a scene on startup ---
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
        // If user cancels or loading fails, exit the application.
        return 0;
    }

    int running = 1;
    while (running) {
        MSG message;
        while (PeekMessage(&message, NULL, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) running = 0;
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
        render_frame();
        HDC device_context = GetDC(g_window_handle);
        StretchDIBits(device_context, 0, 0, g_framebuffer_width, g_framebuffer_height, 0, 0, g_framebuffer_width, g_framebuffer_height,
                      g_framebuffer_memory, &g_framebuffer_info, DIB_RGB_COLORS, SRCCOPY);
        ReleaseDC(g_window_handle, device_context);
    }
    return 0;
}
void render_frame() {
    if (!g_framebuffer_memory) return;
    
    // MODIFIED: Use the loaded sky color
    uint32_t* pixel = (uint32_t*)g_framebuffer_memory;
    for (int i = 0; i < g_framebuffer_width * g_framebuffer_height; ++i) {
        *pixel++ = g_sky_color_uint;
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
    
    mat4_t projection_matrix = mat4_perspective(3.14159f / 4.0f, (float)g_framebuffer_width / (float)g_framebuffer_height, 0.1f, 100.0f);
    
    render_grid(view_matrix, projection_matrix);
    
    for (int i = 0; i < g_scene.object_count; i++) {
        render_object(g_scene.objects[i], i, view_matrix, projection_matrix, camera_pos);
    }
}
void render_object(scene_object_t* object, int object_index, mat4_t view_matrix, mat4_t projection_matrix, vec3_t camera_pos) {
    if (object->light_properties || !object->mesh) {
        return;
    }
    
    mat4_t model_matrix = mat4_get_world_transform(&g_scene, object_index);
    mat4_t final_transform = mat4_mul_mat4(projection_matrix, mat4_mul_mat4(view_matrix, model_matrix));
    
    if (object->mesh->face_count > 0) {
        vec3_t* transformed_vertices = (vec3_t*)malloc(object->mesh->vertex_count * sizeof(vec3_t));
        if (!transformed_vertices) return;
        
        for (int i = 0; i < object->mesh->vertex_count; ++i) {
            vec4_t world_v = mat4_mul_vec4(model_matrix, (vec4_t){object->mesh->vertices[i].x, object->mesh->vertices[i].y, object->mesh->vertices[i].z, 1.0f});
            transformed_vertices[i] = (vec3_t){world_v.x, world_v.y, world_v.z};
        }

        for (int i = 0; i < object->mesh->face_count; ++i) {
            int v0_idx=object->mesh->faces[i*3+0], v1_idx=object->mesh->faces[i*3+1], v2_idx=object->mesh->faces[i*3+2];
            vec3_t v0=transformed_vertices[v0_idx], v1=transformed_vertices[v1_idx], v2=transformed_vertices[v2_idx];
            
            vec3_t normal = vec3_normalize(vec3_cross(vec3_sub(v1, v0), vec3_sub(v2, v0)));
            float dot_product = vec3_dot(normal, vec3_sub(camera_pos, v0));

            if (!object->is_double_sided && dot_product <= 0) {
                continue; 
            }
            
            vec3_t diffuse_color_sum = {0,0,0};
            vec3_t specular_color_sum = {0,0,0};
            float ambient_level = 0.1f;
            diffuse_color_sum = vec3_add(diffuse_color_sum, vec3_scale((vec3_t){1.0f, 1.0f, 1.0f}, ambient_level));
            vec3_t face_center = vec3_scale(vec3_add(v0, vec3_add(v1, v2)), 1.0f/3.0f);

            for (int l = 0; l < g_scene.object_count; l++) {
                scene_object_t* light_obj = g_scene.objects[l];
                if (!light_obj->light_properties) continue;

                mat4_t light_transform = mat4_get_world_transform(&g_scene, l);
                vec3_t light_pos = { light_transform.m[0][3], light_transform.m[1][3], light_transform.m[2][3] };
                vec3_t light_dir = vec3_normalize(vec3_sub(light_pos, face_center));
                float distance_sq = vec3_dot(vec3_sub(light_pos, face_center), vec3_sub(light_pos, face_center));
                if (distance_sq < 1e-6) distance_sq = 1e-6;

                vec3_t current_normal = (dot_product < 0) ? vec3_scale(normal, -1.0f) : normal;
                float diffuse_intensity = vec3_dot(current_normal, light_dir);
                if (diffuse_intensity < 0) diffuse_intensity = 0;

                float attenuation = light_obj->light_properties->intensity / distance_sq;

                if (light_obj->light_properties->type == LIGHT_TYPE_SPOT) {
                    vec4_t local_forward = {0, 0, -1, 0};
                    mat4_t light_rot_matrix = mat4_mul_mat4(mat4_rotation_z(light_obj->rotation.z), mat4_mul_mat4(mat4_rotation_y(light_obj->rotation.y), mat4_rotation_x(light_obj->rotation.x)));
                    vec4_t world_forward_4d = mat4_mul_vec4(light_rot_matrix, local_forward);
                    vec3_t spot_direction = vec3_normalize((vec3_t){world_forward_4d.x, world_forward_4d.y, world_forward_4d.z});

                    float theta = vec3_dot(light_dir, vec3_scale(spot_direction, -1.0f));
                    float epsilon = cosf(light_obj->light_properties->spot_angle / 2.0f);
                    
                    if (theta > epsilon) {
                         float falloff_angle = (light_obj->light_properties->spot_angle / 2.0f) * (1.0f - light_obj->light_properties->spot_blend);
                         float falloff_cos = cosf(falloff_angle);
                         float spot_effect = (theta - epsilon) / (falloff_cos - epsilon);
                         spot_effect = (spot_effect < 0.0f) ? 0.0f : (spot_effect > 1.0f) ? 1.0f : spot_effect;
                         diffuse_intensity *= spot_effect;
                    } else {
                        diffuse_intensity = 0;
                    }
                }

                vec3_t light_contrib = vec3_scale(light_obj->light_properties->color, diffuse_intensity * attenuation);
                diffuse_color_sum = vec3_add(diffuse_color_sum, light_contrib);
                
                // --- NEW: SPECULAR CALCULATION ---
                if (diffuse_intensity > 0.0f && object->material.specular_intensity > 0.0f) {
                    vec3_t view_dir = vec3_normalize(vec3_sub(camera_pos, face_center));
                    vec3_t reflect_dir = vec3_sub(vec3_scale(current_normal, 2.0f * vec3_dot(current_normal, light_dir)), light_dir);
                    float spec_angle = fmax(vec3_dot(view_dir, reflect_dir), 0.0f);
                    float specular_term = powf(spec_angle, object->material.shininess);
                    
                    vec3_t specular_contrib = vec3_scale(light_obj->light_properties->color, specular_term * object->material.specular_intensity * attenuation);
                    specular_color_sum = vec3_add(specular_color_sum, specular_contrib);
                }
            }
            
            // --- MODIFIED: Combine diffuse, specular and apply to material color ---
            vec3_t final_color = {
                object->material.diffuse_color.x * diffuse_color_sum.x + specular_color_sum.x,
                object->material.diffuse_color.y * diffuse_color_sum.y + specular_color_sum.y,
                object->material.diffuse_color.z * diffuse_color_sum.z + specular_color_sum.z
            };
            
            uint8_t r = (uint8_t)(fmin(1.0f, final_color.x) * 255.0f);
            uint8_t g = (uint8_t)(fmin(1.0f, final_color.y) * 255.0f);
            uint8_t b = (uint8_t)(fmin(1.0f, final_color.z) * 255.0f);
            uint32_t face_color = (r << 16) | (g << 8) | b;
            
            vec4_t pv[3];
            pv[0] = mat4_mul_vec4(final_transform, (vec4_t){object->mesh->vertices[v0_idx].x, object->mesh->vertices[v0_idx].y, object->mesh->vertices[v0_idx].z, 1.0f});
            pv[1] = mat4_mul_vec4(final_transform, (vec4_t){object->mesh->vertices[v1_idx].x, object->mesh->vertices[v1_idx].y, object->mesh->vertices[v1_idx].z, 1.0f});
            pv[2] = mat4_mul_vec4(final_transform, (vec4_t){object->mesh->vertices[v2_idx].x, object->mesh->vertices[v2_idx].y, object->mesh->vertices[v2_idx].z, 1.0f});
            draw_filled_triangle(pv[0], pv[1], pv[2], face_color);
        }
        free(transformed_vertices);
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
        for(int j=0;j<4;++j){if(sp[j].w!=0){sp[j].x=(sp[j].x/sp[j].w+1)*0.5f*g_framebuffer_width; sp[j].y=(1-sp[j].y/sp[j].w)*0.5f*g_framebuffer_height; sp[j].z=sp[j].z/sp[j].w;}}
        draw_line(sp[0].x,sp[0].y,sp[0].z, sp[1].x,sp[1].y,sp[1].z, grid_color);
        draw_line(sp[2].x,sp[2].y,sp[2].z, sp[3].x,sp[3].y,sp[3].z, grid_color);
    }
    
    // Draw the main axes
    vec4_t x1={-half_size,0,0,1}, x2={half_size,0,0,1}; // X-axis
    vec4_t y1={0,-half_size,0,1}, y2={0,half_size,0,1}; // Y-axis
    vec4_t z1={0,0,-half_size,1}, z2={0,0,half_size,1}; // Z-axis

    vec4_t axis_p[6] = {mat4_mul_vec4(vp_matrix,x1), mat4_mul_vec4(vp_matrix,x2), mat4_mul_vec4(vp_matrix,y1), mat4_mul_vec4(vp_matrix,y2), mat4_mul_vec4(vp_matrix,z1), mat4_mul_vec4(vp_matrix,z2)};
    for(int j=0;j<6;++j){if(axis_p[j].w!=0){axis_p[j].x=(axis_p[j].x/axis_p[j].w+1)*0.5f*g_framebuffer_width; axis_p[j].y=(1-axis_p[j].y/axis_p[j].w)*0.5f*g_framebuffer_height; axis_p[j].z=axis_p[j].z/axis_p[j].w;}}
    
    draw_line(axis_p[0].x,axis_p[0].y,axis_p[0].z, axis_p[1].x,axis_p[1].y,axis_p[1].z, axis_color_x); // Red X
    draw_line(axis_p[2].x,axis_p[2].y,axis_p[2].z, axis_p[3].x,axis_p[3].y,axis_p[3].z, axis_color_y); // Green Y
    draw_line(axis_p[4].x,axis_p[4].y,axis_p[4].z, axis_p[5].x,axis_p[5].y,axis_p[5].z, axis_color_z); // Blue Z
}
// --- Drawing Helper Functions ---
void draw_pixel(int x,int y,float z,uint32_t c){if(x>=0&&x<g_framebuffer_width&&y>=0&&y<g_framebuffer_height){int i=x+y*g_framebuffer_width;if(z<g_depth_buffer[i]){*((uint32_t*)g_framebuffer_memory+i)=c;g_depth_buffer[i]=z;}}}
void draw_line(int x0,int y0,float z0,int x1,int y1,float z1,uint32_t c){int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy,e2;float z=z0,dz=(z1-z0)/sqrtf((float)(x1-x0)*(x1-x0)+(y1-y0)*(y1-y0));for(;;){draw_pixel(x0,y0,z,c);if(x0==x1&&y0==y1)break;e2=2*err;if(e2>=dy){err+=dy;x0+=sx;z+=dz*sx;}if(e2<=dx){err+=dx;y0+=sy;z+=dz*sy;}}}
void draw_filled_triangle(vec4_t p0,vec4_t p1,vec4_t p2,uint32_t c){if(p0.w<=0||p1.w<=0||p2.w<=0)return;p0.x/=p0.w;p0.y/=p0.w;p0.z/=p0.w;p1.x/=p1.w;p1.y/=p1.w;p1.z/=p1.w;p2.x/=p2.w;p2.y/=p2.w;p2.z/=p2.w;p0.x=(p0.x+1)*0.5f*g_framebuffer_width;p0.y=(1-p0.y)*0.5f*g_framebuffer_height;p1.x=(p1.x+1)*0.5f*g_framebuffer_width;p1.y=(1-p1.y)*0.5f*g_framebuffer_height;p2.x=(p2.x+1)*0.5f*g_framebuffer_width;p2.y=(1-p2.y)*0.5f*g_framebuffer_height;if(p0.y>p1.y){vec4_t t=p0;p0=p1;p1=t;}if(p0.y>p2.y){vec4_t t=p0;p0=p2;p2=t;}if(p1.y>p2.y){vec4_t t=p1;p1=p2;p2=t;}if((int)p0.y==(int)p2.y)return;float p0w=1/p0.w,p1w=1/p1.w,p2w=1/p2.w;for(int y=(int)p0.y;y<=(int)p2.y;y++){if(y<0||y>=g_framebuffer_height)continue;float a=(p2.y-p0.y==0)?1:(float)(y-p0.y)/(p2.y-p0.y);int xa=(int)(p0.x+a*(p2.x-p0.x));float wa=p0w+a*(p2w-p0w);int xb;float wb;if(y<p1.y){float b=(p1.y-p0.y==0)?1:(float)(y-p0.y)/(p1.y-p0.y);xb=(int)(p0.x+b*(p1.x-p0.x));wb=p0w+b*(p1w-p0w);}else{float b=(p2.y-p1.y==0)?1:(float)(y-p1.y)/(p2.y-p1.y);xb=(int)(p1.x+b*(p2.x-p1.x));wb=p1w+b*(p2w-p1w);}if(xa>xb){int t=xa;xa=xb;xb=t;float tw=wa;wa=wb;wb=tw;}for(int x=xa;x<=xb;x++){if(x<0||x>=g_framebuffer_width)continue;float t=(xb-xa==0)?1:(float)(x-xa)/(xb-xa);float w=wa+t*(wb-wa);if(w>0)draw_pixel(x,y,1/w,c);}}}

LRESULT CALLBACK window_callback(HWND window_handle, UINT message, WPARAM w_param, LPARAM l_param) {
    LRESULT result = 0;
    switch (message) {
        case WM_CLOSE: case WM_DESTROY: {
            scene_destroy(&g_scene);
            PostQuitMessage(0);
        } break;
        case WM_SIZE: {
            g_framebuffer_width = LOWORD(l_param); g_framebuffer_height = HIWORD(l_param);
            
            // --- MODIFIED: Use CreateDIBSection for a more robust framebuffer ---
            HBITMAP g_framebuffer_bitmap; // This can be local as we only need it to draw
            if (g_framebuffer_bitmap) DeleteObject(g_framebuffer_bitmap);
            if (g_depth_buffer) free(g_depth_buffer);

            g_framebuffer_info.bmiHeader.biSize = sizeof(g_framebuffer_info.bmiHeader);
            g_framebuffer_info.bmiHeader.biWidth = g_framebuffer_width;
            g_framebuffer_info.bmiHeader.biHeight = -g_framebuffer_height; // Top-down DIB
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
            
            g_depth_buffer = (float*)malloc(g_framebuffer_width * g_framebuffer_height * sizeof(float));
            // --- END MODIFICATION ---

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
            int delta = GET_WHEEL_DELTA_WPARAM(w_param);
            if (delta > 0) g_camera_distance -= 0.5f; else g_camera_distance += 0.5f;
            if (g_camera_distance < 2.0f) g_camera_distance = 2.0f;
            if (g_camera_distance > 40.0f) g_camera_distance = 40.0f;
        } break;
        case WM_MOUSEMOVE: {
            int cur_x = LOWORD(l_param), cur_y = HIWORD(l_param);
            int dx = cur_x - g_last_mouse_x, dy = cur_y - g_last_mouse_y;
            if (g_middle_mouse_down && (GetKeyState(VK_SHIFT) & 0x8000)) {
                // --- MODIFIED: Z-Up Panning Logic ---
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
                // --- END MODIFICATION ---
            } else if (g_middle_mouse_down) {
                g_camera_yaw += (float)dx * 0.01f;
                g_camera_pitch += (float)dy * 0.01f;
                if (g_camera_pitch > 1.5f) g_camera_pitch = 1.5f;
                if (g_camera_pitch < -1.5f) g_camera_pitch = -1.5f;
            }
            g_last_mouse_x = cur_x;
            g_last_mouse_y = cur_y;
        } break;
        case WM_KEYDOWN: {
            if (w_param == VK_ESCAPE) {
                PostQuitMessage(0);
            }
        } break;
        default: {
            result = DefWindowProc(window_handle, message, w_param, l_param);
        }
    }
    return result;
}
