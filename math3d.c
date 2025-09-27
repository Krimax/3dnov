// math3d.c
// Complete implementation of our 3D math library with all functions.

#include "math3d.h"
#include <math.h>

// --- Original Matrix Functions ---

mat4_t mat4_identity(void) {
    mat4_t m = {0};
    m.m[0][0] = 1.0f; m.m[1][1] = 1.0f; m.m[2][2] = 1.0f; m.m[3][3] = 1.0f;
    return m;
}

mat4_t mat4_perspective(float fov_y, float aspect_ratio, float near_plane, float far_plane) {
    mat4_t m = {0};
    float tan_half_fovy = tanf(fov_y / 2.0f);
    m.m[0][0] = 1.0f / (aspect_ratio * tan_half_fovy);
    m.m[1][1] = 1.0f / tan_half_fovy;
    m.m[2][2] = -(far_plane + near_plane) / (far_plane - near_plane);
    m.m[2][3] = -1.0f;
    m.m[3][2] = -(2.0f * far_plane * near_plane) / (far_plane - near_plane);
    return m;
}

vec4_t mat4_mul_vec4(mat4_t m, vec4_t v) {
    vec4_t result;
    result.x = m.m[0][0] * v.x + m.m[0][1] * v.y + m.m[0][2] * v.z + m.m[0][3] * v.w;
    result.y = m.m[1][0] * v.x + m.m[1][1] * v.y + m.m[1][2] * v.z + m.m[1][3] * v.w;
    result.z = m.m[2][0] * v.x + m.m[2][1] * v.y + m.m[2][2] * v.z + m.m[2][3] * v.w;
    result.w = m.m[3][0] * v.x + m.m[3][1] * v.y + m.m[3][2] * v.z + m.m[3][3] * v.w;
    return result;
}

mat4_t mat4_mul_mat4(mat4_t a, mat4_t b) {
    mat4_t result = {0};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            result.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
        }
    }
    return result;
}

mat4_t mat4_translation(float tx, float ty, float tz) {
    mat4_t m = mat4_identity();
    m.m[0][3] = tx; m.m[1][3] = ty; m.m[2][3] = tz;
    return m;
}

mat4_t mat4_rotation_x(float angle_rad) {
    mat4_t m = mat4_identity();
    float c = cosf(angle_rad); float s = sinf(angle_rad);
    m.m[1][1] = c; m.m[1][2] = -s; m.m[2][1] = s; m.m[2][2] = c;
    return m;
}

mat4_t mat4_rotation_y(float angle_rad) {
    mat4_t m = mat4_identity();
    float c = cosf(angle_rad); float s = sinf(angle_rad);
    m.m[0][0] = c; m.m[0][2] = s; m.m[2][0] = -s; m.m[2][2] = c;
    return m;
}

mat4_t mat4_rotation_z(float angle_rad) {
    mat4_t m = mat4_identity();
    float c = cosf(angle_rad); float s = sinf(angle_rad);
    m.m[0][0] = c; m.m[0][1] = -s; m.m[1][0] = s; m.m[1][1] = c;
    return m;
}


// --- NEW Vector Functions ---

vec3_t vec3_sub(vec3_t a, vec3_t b) {
    vec3_t result = { a.x - b.x, a.y - b.y, a.z - b.z };
    return result;
}

vec3_t vec3_cross(vec3_t a, vec3_t b) {
    vec3_t result = {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
    return result;
}

float vec3_dot(vec3_t a, vec3_t b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

vec3_t vec3_normalize(vec3_t v) {
    float length = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (length != 0.0f) {
        v.x /= length;
        v.y /= length;
        v.z /= length;
    }
    return v;
}
mat4_t mat4_look_at(vec3_t eye, vec3_t target, vec3_t up) {
    // Calculate the forward, right, and up vectors for the camera's coordinate system
    vec3_t z_axis = vec3_normalize(vec3_sub(target, eye)); // The "forward" vector
    vec3_t x_axis = vec3_normalize(vec3_cross(z_axis, up)); // The "right" vector
    vec3_t y_axis = vec3_cross(x_axis, z_axis);             // The "up" vector

    // Create the view matrix
    mat4_t view_matrix = {
        .m = {
            { x_axis.x, x_axis.y, x_axis.z, -vec3_dot(x_axis, eye) },
            { y_axis.x, y_axis.y, y_axis.z, -vec3_dot(y_axis, eye) },
            { -z_axis.x, -z_axis.y, -z_axis.z, vec3_dot(z_axis, eye) }, // Invert Z for right-handed coordinate system
            { 0, 0, 0, 1 }
        }
    };

    return view_matrix;
}
mat4_t mat4_inverse(mat4_t m) {
    mat4_t inv;
    float det;
    int i;

    inv.m[0][0] = m.m[1][1]  * m.m[2][2] * m.m[3][3] - 
                 m.m[1][1]  * m.m[2][3] * m.m[3][2] - 
                 m.m[2][1]  * m.m[1][2] * m.m[3][3] + 
                 m.m[2][1]  * m.m[1][3] * m.m[3][2] +
                 m.m[3][1] * m.m[1][2] * m.m[2][3] - 
                 m.m[3][1] * m.m[1][3] * m.m[2][2];

    inv.m[1][0] = -m.m[1][0]  * m.m[2][2] * m.m[3][3] + 
                  m.m[1][0]  * m.m[2][3] * m.m[3][2] + 
                  m.m[2][0]  * m.m[1][2] * m.m[3][3] - 
                  m.m[2][0]  * m.m[1][3] * m.m[3][2] - 
                  m.m[3][0] * m.m[1][2] * m.m[2][3] + 
                  m.m[3][0] * m.m[1][3] * m.m[2][2];

    inv.m[2][0] = m.m[1][0]  * m.m[2][1] * m.m[3][3] - 
                 m.m[1][0]  * m.m[2][3] * m.m[3][1] - 
                 m.m[2][0]  * m.m[1][1] * m.m[3][3] + 
                 m.m[2][0]  * m.m[1][3] * m.m[3][1] + 
                 m.m[3][0] * m.m[1][1] * m.m[2][3] - 
                 m.m[3][0] * m.m[1][3] * m.m[2][1];

    inv.m[3][0] = -m.m[1][0]  * m.m[2][1] * m.m[3][2] + 
                   m.m[1][0]  * m.m[2][2] * m.m[3][1] +
                   m.m[2][0]  * m.m[1][1] * m.m[3][2] - 
                   m.m[2][0]  * m.m[1][2] * m.m[3][1] - 
                   m.m[3][0] * m.m[1][1] * m.m[2][2] + 
                   m.m[3][0] * m.m[1][2] * m.m[2][1];

    inv.m[0][1] = -m.m[0][1]  * m.m[2][2] * m.m[3][3] + 
                  m.m[0][1]  * m.m[2][3] * m.m[3][2] + 
                  m.m[2][1]  * m.m[0][2] * m.m[3][3] - 
                  m.m[2][1]  * m.m[0][3] * m.m[3][2] - 
                  m.m[3][1] * m.m[0][2] * m.m[2][3] + 
                  m.m[3][1] * m.m[0][3] * m.m[2][2];

    inv.m[1][1] = m.m[0][0]  * m.m[2][2] * m.m[3][3] - 
                 m.m[0][0]  * m.m[2][3] * m.m[3][2] - 
                 m.m[2][0]  * m.m[0][2] * m.m[3][3] + 
                 m.m[2][0]  * m.m[0][3] * m.m[3][2] + 
                 m.m[3][0] * m.m[0][2] * m.m[2][3] - 
                 m.m[3][0] * m.m[0][3] * m.m[2][2];

    inv.m[2][1] = -m.m[0][0]  * m.m[2][1] * m.m[3][3] + 
                  m.m[0][0]  * m.m[2][3] * m.m[3][1] + 
                  m.m[2][0]  * m.m[0][1] * m.m[3][3] - 
                  m.m[2][0]  * m.m[0][3] * m.m[3][1] - 
                  m.m[3][0] * m.m[0][1] * m.m[2][3] + 
                  m.m[3][0] * m.m[0][3] * m.m[2][1];

    inv.m[3][1] = m.m[0][0]  * m.m[2][1] * m.m[3][2] - 
                  m.m[0][0]  * m.m[2][2] * m.m[3][1] - 
                  m.m[2][0]  * m.m[0][1] * m.m[3][2] + 
                  m.m[2][0]  * m.m[0][2] * m.m[3][1] + 
                  m.m[3][0] * m.m[0][1] * m.m[2][2] - 
                  m.m[3][0] * m.m[0][2] * m.m[2][1];

    inv.m[0][2] = m.m[0][1]  * m.m[1][2] * m.m[3][3] - 
                 m.m[0][1]  * m.m[1][3] * m.m[3][2] - 
                 m.m[1][1]  * m.m[0][2] * m.m[3][3] + 
                 m.m[1][1]  * m.m[0][3] * m.m[3][2] + 
                 m.m[3][1] * m.m[0][2] * m.m[1][3] - 
                 m.m[3][1] * m.m[0][3] * m.m[1][2];

    inv.m[1][2] = -m.m[0][0]  * m.m[1][2] * m.m[3][3] + 
                  m.m[0][0]  * m.m[1][3] * m.m[3][2] + 
                  m.m[1][0]  * m.m[0][2] * m.m[3][3] - 
                  m.m[1][0]  * m.m[0][3] * m.m[3][2] - 
                  m.m[3][0] * m.m[0][2] * m.m[1][3] + 
                  m.m[3][0] * m.m[0][3] * m.m[1][2];

    inv.m[2][2] = m.m[0][0]  * m.m[1][1] * m.m[3][3] - 
                 m.m[0][0]  * m.m[1][3] * m.m[3][1] - 
                 m.m[1][0]  * m.m[0][1] * m.m[3][3] + 
                 m.m[1][0]  * m.m[0][3] * m.m[3][1] + 
                 m.m[3][0] * m.m[0][1] * m.m[1][3] - 
                 m.m[3][0] * m.m[0][3] * m.m[1][1];

    inv.m[3][2] = -m.m[0][0]  * m.m[1][1] * m.m[3][2] + 
                   m.m[0][0]  * m.m[1][2] * m.m[3][1] + 
                   m.m[1][0]  * m.m[0][1] * m.m[3][2] - 
                   m.m[1][0]  * m.m[0][2] * m.m[3][1] - 
                   m.m[3][0] * m.m[0][1] * m.m[1][2] + 
                   m.m[3][0] * m.m[0][2] * m.m[1][1];

    inv.m[0][3] = -m.m[0][1] * m.m[1][2] * m.m[2][3] + 
                  m.m[0][1] * m.m[1][3] * m.m[2][2] + 
                  m.m[1][1] * m.m[0][2] * m.m[2][3] - 
                  m.m[1][1] * m.m[0][3] * m.m[2][2] - 
                  m.m[2][1] * m.m[0][2] * m.m[1][3] + 
                  m.m[2][1] * m.m[0][3] * m.m[1][2];

    inv.m[1][3] = m.m[0][0] * m.m[1][2] * m.m[2][3] - 
                 m.m[0][0] * m.m[1][3] * m.m[2][2] - 
                 m.m[1][0] * m.m[0][2] * m.m[2][3] + 
                 m.m[1][0] * m.m[0][3] * m.m[2][2] + 
                 m.m[2][0] * m.m[0][2] * m.m[1][3] - 
                 m.m[2][0] * m.m[0][3] * m.m[1][2];

    inv.m[2][3] = -m.m[0][0] * m.m[1][1] * m.m[2][3] + 
                   m.m[0][0] * m.m[1][3] * m.m[2][1] + 
                   m.m[1][0] * m.m[0][1] * m.m[2][3] - 
                   m.m[1][0] * m.m[0][3] * m.m[2][1] - 
                   m.m[2][0] * m.m[0][1] * m.m[1][3] + 
                   m.m[2][0] * m.m[0][3] * m.m[1][1];

    inv.m[3][3] = m.m[0][0] * m.m[1][1] * m.m[2][2] - 
                  m.m[0][0] * m.m[1][2] * m.m[2][1] - 
                  m.m[1][0] * m.m[0][1] * m.m[2][2] + 
                  m.m[1][0] * m.m[0][2] * m.m[2][1] + 
                  m.m[2][0] * m.m[0][1] * m.m[1][2] - 
                  m.m[2][0] * m.m[0][2] * m.m[1][1];

    det = m.m[0][0] * inv.m[0][0] + m.m[0][1] * inv.m[1][0] + m.m[0][2] * inv.m[2][0] + m.m[0][3] * inv.m[3][0];

    if (det == 0)
        return mat4_identity(); // Cannot inverse, return identity

    det = 1.0f / det;

    for (i = 0; i < 16; i++)
        ((float*)inv.m)[i] = ((float*)inv.m)[i] * det;

    return inv;
}
vec3_t vec3_add(vec3_t a, vec3_t b) {
    vec3_t result = { a.x + b.x, a.y + b.y, a.z + b.z };
    return result;
}
mat4_t mat4_scale(float sx, float sy, float sz) {
    mat4_t m = mat4_identity();
    m.m[0][0] = sx;
    m.m[1][1] = sy;
    m.m[2][2] = sz;
    return m;
}
mat4_t mat4_get_world_transform(const scene_t* scene, int object_index) {
    if (object_index < 0 || object_index >= scene->object_count) {
        return mat4_identity(); // Return identity if index is invalid
    }

    scene_object_t* object = scene->objects[object_index];

    // 1. Calculate the object's local transform matrix from its TRS properties
    mat4_t translation_matrix = mat4_translation(object->position.x, object->position.y, object->position.z);
    mat4_t rot_x_matrix = mat4_rotation_x(object->rotation.x);
    mat4_t rot_y_matrix = mat4_rotation_y(object->rotation.y);
    mat4_t rot_z_matrix = mat4_rotation_z(object->rotation.z);
    mat4_t scale_matrix = mat4_scale(object->scale.x, object->scale.y, object->scale.z);

    mat4_t rotation_matrix = mat4_mul_mat4(rot_z_matrix, mat4_mul_mat4(rot_y_matrix, rot_x_matrix));
    mat4_t local_transform = mat4_mul_mat4(translation_matrix, mat4_mul_mat4(scale_matrix, rotation_matrix));

    // 2. If the object has a parent, recursively get the parent's world transform
    if (object->parent_index != -1) {
        mat4_t parent_world_transform = mat4_get_world_transform(scene, object->parent_index);
        // 3. Combine the parent's transform with this object's local transform
        return mat4_mul_mat4(parent_world_transform, local_transform);
    } else {
        // 4. If there's no parent, the local transform is the world transform
        return local_transform;
    }
}
vec3_t vec3_scale(vec3_t v, float s) {
    vec3_t result = { v.x * s, v.y * s, v.z * s };
    return result;
}

