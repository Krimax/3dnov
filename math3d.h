// math3d.h (Updated for Camera Control)

#ifndef MATH3D_H
#define MATH3D_H
#include <windows.h>
// --- Structures ---
typedef struct { float x, y, z; } vec3_t;
typedef struct { float x, y, z, w; } vec4_t;
typedef struct { float m[4][4]; } mat4_t;
typedef struct {
    vec3_t* vertices;   // Dynamic array of vertices
    int* faces;         // Dynamic array of face indices (3 per triangle)
    int vertex_count;
    int face_count;
} mesh_t;
typedef struct {
    mesh_t* mesh;       // Pointer to the shared mesh data
    vec3_t position;    // Object's position in the world
    vec3_t rotation;    // Euler angles for rotation (in radians)
    vec3_t scale;       // Scale along each axis
    
    // --- HIERARCHY FIELDS ---
    int parent_index;   // Index of the parent object in the scene's objects array (-1 for none)
    int* children;      // Dynamic array of indices for child objects
    int child_count;
    int child_capacity;

    // --- UI & PROPERTIES ---
    char name[64];      // Name for the object, to be displayed in the UI
    RECT ui_outliner_rect; // The clickable screen-space rect for this object in the outliner
    int is_double_sided; // 0 = Backface Culling (Default), 1 = Render both sides
} scene_object_t;

typedef struct {
    scene_object_t** objects; // Dynamic array of pointers to scene objects
    int object_count;
    int capacity;
} scene_t;

// --- Vector Functions ---
vec3_t vec3_sub(vec3_t a, vec3_t b);
vec3_t vec3_cross(vec3_t a, vec3_t b);
float vec3_dot(vec3_t a, vec3_t b);
vec3_t vec3_normalize(vec3_t v);
vec3_t vec3_add(vec3_t a, vec3_t b);
vec3_t vec3_scale(vec3_t v, float s);
// --- Matrix Functions ---
mat4_t mat4_identity(void);
mat4_t mat4_perspective(float fov_y, float aspect_ratio, float near_plane, float far_plane);
mat4_t mat4_translation(float tx, float ty, float tz);
mat4_t mat4_rotation_x(float angle_rad);
mat4_t mat4_rotation_y(float angle_rad);
mat4_t mat4_rotation_z(float angle_rad);
vec4_t mat4_mul_vec4(mat4_t m, vec4_t v);
mat4_t mat4_mul_mat4(mat4_t a, mat4_t b);
mat4_t mat4_look_at(vec3_t eye, vec3_t target, vec3_t up);
mat4_t mat4_inverse(mat4_t m);
mat4_t mat4_scale(float sx, float sy, float sz);
mat4_t mat4_get_world_transform(const scene_t* scene, int object_index);
mat4_t mat4_orthographic(float left, float right, float bottom, float top, float near_plane, float far_plane);

#endif // MATH3D_H
