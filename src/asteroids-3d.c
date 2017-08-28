/*****************************************************************************
 * Simple Asteroids 3D
 * Version 1
 *
 * A simple 3D interpretation of 'Asteroids'
 *
 * https://github.com/dseguin/asteroids-3d/
 * Copyright (c) 2017 David Seguin <davidseguin@live.ca>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *****************************************************************************/

#ifdef _WIN32
  #pragma comment(lib, "opengl32.lib")
  #include <Windows.h>
#endif
#include <GL/gl.h>
#include <SDL.h>
#ifdef _WIN32
  #include <SDL_main.h>
  #include <SDL_opengl_glext.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __GNUC__
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wcast-qual"
  #pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ASSERT(x) /*don't use assert.h*/
#include "../ext/stb/stb_image.h"
#ifdef __GNUC__
  #pragma GCC diagnostic pop
#endif

#ifndef M_PI
  #ifdef M_PIl
    #define M_PI    M_PIl
  #else
    #pragma message "No definition for PI. Using local def."
    #define M_PI    3.14159265
  #endif
#endif

#define SQRT_TOLERANCE 0.001f
#define ARENA_SIZE     500.f /*from center to edge of arena*/
#define MAX_SHOTS      8
#define MAX_ASTEROIDS  64
#define INIT_ASTEROIDS 32
#define ASTER_LARGE    10
#define ASTER_MED      5
#define ASTER_SMALL    1
#define true           '\x01'
#define false          '\x00'

#define BITFONT_OFFSET(x) (256*(7 - (x/16)) + (x%16))

const float radmod = M_PI/180.f;
const float target_time = 50.f/3.f;

/*1-byte boolean*/
typedef unsigned char bool;

/*function pointers for ARB_vertex_buffer_object*/
typedef void (APIENTRY *glDeleteBuffersARB_Func)(GLsizei    n,
                                              const GLuint *buffers);
typedef void (APIENTRY *glGenBuffersARB_Func)(GLsizei       n,
                                              GLuint       *buffers);
typedef void (APIENTRY *glBindBufferARB_Func)(GLenum        target,
                                              GLuint        buffer);
typedef void (APIENTRY *glBufferDataARB_Func)(GLenum        target,
                                              GLsizeiptr    size,
                                              const GLvoid *data,
                                              GLenum        usage);
glDeleteBuffersARB_Func glDeleteBuffersARB_ptr = 0;
glGenBuffersARB_Func glGenBuffersARB_ptr = 0;
glBindBufferARB_Func glBindBufferARB_ptr = 0;
glBufferDataARB_Func glBufferDataARB_ptr = 0;

/*** Model object ***
 *
 * Struct containing 3D model data.
 *
 * Index data assumes GL_UNSIGNED_INT type
 * and GL_TRIANGLES drawing order.
 *
 * Vertex data assumes 6 floats per vertex:
 * Normal vector(x,y,z) + Vertex position(x,y,z)
 * in that order.
 **/
typedef struct A3DModel {
    char     *file_root;
    unsigned *index_data;  /*don't expect to */
    float    *vertex_data; /*access these directly*/
    int       index_count;
    int       vertex_count;
    int       index_offset;
    int       vertex_offset;
} A3DModel;

/*** Actor properties ***
 *
 * Struct containing physical properties of an object.
 *
 * 'pos' and 'quat_orientation' represent the current position
 * and orientation of the object. 'vel' and 'euler_rot' represent
 * incremental change in velocity and rotation. 'mass' is used
 * currently to indicate size.
 **/
typedef struct A3DActor {
    bool      is_spawned;
    float     mass;
    struct {
        float x;
        float y;
        float z;
    } pos;
    struct {
        float x;
        float y;
        float z;
    } vel;
    struct {
        float x;
        float y;
        float z;
        float w;
    } quat_orientation;
    struct {
        float yaw;
        float pitch;
        float roll;
    } euler_rot;
} A3DActor;

/*** Camera object ***
 *
 * Contains camera parameters.
 *
 * 'player' is the actor representing the camera/player.
 * Several booleans determine if certain keys are pressed
 * that are used for camera movement and orientation. Other
 * modifiers affect camera control variables.
 **/
typedef struct A3DCamera {
    A3DActor *player;
    bool      forward;
    bool      backward;
    bool      left;
    bool      right;
    bool      up;
    bool      down;
    bool      ccw;
    bool      cw;
    bool      shoot;
    bool      driftcam;      /*camera drift from mouse motion*/
    float     fovmod;
    float     rotmod;
    float     rollmod;
    float     velmod;
    float     sens;          /*sensitivity*/
    float     pos_offset[3]; /*driftcam position*/
    float     roll;          /*driftcam roll*/
} A3DCamera;

/*** Popup score text ***
 *
 * Text that pops up after hitting an asteroid.
 * 'offset' serves as a timer.
 **/
typedef struct A3DScoreText {
    bool      is_spawned;
    char      text[8];
    float     offset;
    struct {
        float x;
        float y;
        float z;
    } pos;
} A3DScoreText;

/*** Image object ***
 *
 * Struct with properties for use with stb_image.h.
 * The image filename is set early on, everything else
 * is set by stbi_load(). 'depth' refers to the number
 * of 8 bit components of the image. 'offset' is the
 * position of the image in the buffer object.
 **/
typedef struct A3DImage {
    char          *filename;
    unsigned char *data;
    int            width;
    int            height;
    int            depth;
    int            offset;
} A3DImage;

/*** Reset game objects ***
 *
 * Resets the player and asteroids.
 *
 *     player - player actor object.
 *     aster  - array of asteroid actors.
 *
 * After the player dies and the blast effect is done
 * growing, the positions, orientations, rotations, and
 * velocities of the game actors are reset to their
 * initial values.
 **/
void reset_game(A3DActor *player, A3DActor *aster);

/*** Get projectile velocity ***
 *
 * Uses quat_orientation to determine velocity vector.
 *
 *     obj - projectile actor object.
 *
 * This is a quick hack to rotate a projectile's velocity
 * vector from the object's quat_orientation (assuming it
 * was set to the player's position/orientation beforehand).
 * Velocity magnitude is taken from the vel.z value.
 **/
void get_shot_vel(A3DActor *obj);

/*** Transform static object ***
 *
 * Apply transformations to an object in constant motion.
 *
 *     obj - static actor object.
 *     dt  - frame time modifier.
 *
 * Uses rotate_static_actor() and translate_static_actor()
 * to generate a 4x4 transform matrix, then calls glMultMatrix().
 * To keep the transform increments consistent whether VSync is
 * enabled or disabled, a frame time modifier is needed. The 'dt'
 * modifier should be
 *
 *   min(previous frametime, target frametime)/(target frametime)
 **/
void transform_static_actor(A3DActor *obj, float dt);
void rotate_static_actor   (A3DActor *obj, float *m, float dt);
void translate_static_actor(A3DActor *obj, float *m, float dt);

/*** Move camera ***
 *
 * Rotates and translates camera.
 *
 *     cam - player camera object.
 *     dt  - frame time modifier.
 *
 * This replaces the glRotatef() and glTranslatef() calls
 * in the drawing section. move_camera() takes care of calling
 * glMultMatrix() to update camera position and orientation.
 *
 * The change in rotation is taken from the player actor's
 * rotation in Euler angles and gets converted to a quaternion,
 * which is multiplied against the player's orientation in the
 * order Qc*Qo. The resulting quaternion is used to generate
 * a (transposed) rotation matrix. If direction keys are
 * pressed, calculate the new velocity vectors from the matrix
 * and update translation component of the current matrix,
 * then call glMultMatrix().
 * To keep the transform increments consistent whether VSync is
 * enabled or disabled, a frame time modifier is needed. The 'dt'
 * modifier should be
 *
 *   min(previous frametime, target frametime)/(target frametime)
 **/
void move_camera(A3DCamera *cam, float dt);

/*** Get inverse sqrt ***
 *
 * Returns the resulting inverse square root.
 *
 * This is the improved version optimized by
 * Douglas Wilhelm Harder that can be found here:
 * <https://ece.uwaterloo.ca/~dwharder/aads/Algorithms/Inverse_square_root/>
 *
 * This version includes a multiplier to improve
 * the result of Newton's method.
 **/
float inv_sqrt_dwh(float x);

/*** Load model from file ***
 *
 * Loads vertex and index data from file.
 *
 *     file_prefix - Path to model data, minus
 *                   extension.
 *     model       - Struct to contain vertex and
 *                   index data.
 *
 * Returns 0 if successful, 1 if otherwise.
 *
 * 'file_prefix' is the name of the model appended to the
 * path containing its data files. E.g. the 'pumpkin' object
 * has a file_prefix of "data/pumpkin", where vertex data is
 * taken from "data/pumpkin.nv", the index data is taken from
 * "data/pumpkin.ix", and model metadata is taken from
 * "data/pumpkin.met".
 *
 * The "*.ix" and "*.nv" files contain raw array data for
 * index and vertex respectively. Vertex data contains normal
 * vectors and vertex positions for each vertex, amounting to
 * 6 floats per vertex. Index data is stored in single
 * unsigned ints. The "*.met" files contain metadata for the
 * object, like vertex count and index count. "*.ix" and "*.nv"
 * files are intended to be written directly to arrays to be
 * used in GL buffer objects.
 *
 * Index and vertex data is also checksumed in a *very* basic
 * hashing implementation to provide some *very* basic
 * verification.
 *
 * Vertex and index data returned from this function are
 * malloc-ed at runtime, and they need to be freed manually.
 **/
bool load_model_from_file(const char *file_prefix, A3DModel *model);

/*** Load models ***
 *
 * Takes care of loading models and copying vertex/index
 * data to buffer objects.
 *
 *     model    - Array of A3DModel where file_root
 *                has been set.
 *     count    - Number of models in the array.
 *
 * Returns true if successful, false if otherwise.
 *
 * Each call to load_models clears the VBOs from the last
 * call and reinitializes them with new models in model.
 * Memory allocation/deallocation is handled internally, so
 * no memory management of vertex/index data is necessary.
 **/
bool load_models(A3DModel **model, const int count);

/*** Draw model ***
 *
 * Draws specified model.
 *
 *     model - Model to draw
 *
 * Changes the vertex buffer offset to the specified model
 * and draws the model using glDrawElements() with the
 * associated index offset.
 *
 * This assumes the drawing mode is GL_TRIANGLES and the
 * index packing is GL_UNSIGNED_INT.
 **/
void draw_model(const A3DModel model);

/*** Draw skybox ***
 *
 * Draws a textured skybox.
 *
 *     skybox  - Properties of the texture image
 *     x, y, z - Center coordinates
 *
 * Draws 6 quads that are textured with the skybox image
 * forming a box around the player. The center should be the
 * negative of the player's current position.
 **/
void draw_skybox(const A3DImage skybox,
                 const float x, const float y, const float z);

int main(void)
{
    /*vars*/
    bool          loop_exit      = false,
                  skip_dt        = false,
                  fullscreen     = false;
    char          win_title[256] = {'\0'},
                  t_fps[16]      = {'\0'},
                  t_mspf[16]     = {'\0'},
                  t_relvel[32]   = {'\0'},
                  t_score[32]    = {'\0'},
                  t_topscore[32] = {'\0'},
                 *basepath;
    float         aspect_ratio   = 1.f,
                  fov            = 80.f,
                  top_clip       = 0.f,
                  bottom_clip    = 0.f,
                  left_clip      = 0.f,
                  right_clip     = 0.f,
                  near_clip      = 1.f,
                  far_clip       = 800.f,
                  shot_speed     = 5.f,
                  frametime      = -1.f,
                  mintime        = 0.f,
                  timemod        = 1.f,
                  blastmod       = 32.f;
    float         tmp_diffuse_color[] = {0.f, 0.8f, 0.f, 1.f};
    int           i,j,k,
                  width_real,
                  height_real,
                  debug_level      = 1;
    unsigned      shot_loop_count  = 0,
                  spawn_loop_count = 0,
                  title_loop_count = 0,
                  currtime         = 0,
                  prevtime         = 0,
                  difftime         = 0,
                  score            = 0,
                  topscore         = 0;
    SDL_Event     ev_main;
    SDL_Window   *win_main;
    SDL_GLContext win_main_gl;
    A3DActor      a_player = {
                    true, 1.f,
                    {0.f,0.f,0.f},
                    {0.f,0.f,0.f},
                    {0.f,0.f,0.f,1.f},
                    {0.f,0.f,0.f}};
    A3DActor      a_blast = {
                    false, 1.f,
                    {0.f,0.f,0.f},
                    {0.f,0.f,0.f},
                    {0.f,0.f,0.f,1.f},
                    {0.f,0.f,0.f}};
    A3DActor     *a_shot;
    A3DActor     *a_aster;
    A3DCamera     camera = {
                    NULL, false, false, false, false,
                    false, false, false, false, false,
                    true, 1.f, 0.005f, 7.f, 0.008f,
                    0.8f, {0.f, -2.f, -5.f}, 0.f};
    A3DModel      m_player,
                  m_projectile,
                  m_asteroid,
                  m_blast,
                  m_boundbox,
                 *m_ptr_all[5];
    A3DImage      i_font,
                  i_skybox;
    A3DScoreText  scoretext[3] = {{false, {'\0'}, 0.f, {0.f, 0.f, 0.f}}};
    A3DScoreText  reticule[3]  = {{true,  {'\0'}, 0.f, {0.f, 0.f, 0.f}}};

    /*initialize projectiles*/
    a_shot = malloc(sizeof(A3DActor)*MAX_SHOTS);
    for(i = 0; i < MAX_SHOTS; i++)
    {
        a_shot[i].is_spawned         = false;
        a_shot[i].mass               = 0.f;
        a_shot[i].pos.x              = 0.f;
        a_shot[i].pos.y              = 0.f;
        a_shot[i].pos.z              = 0.f;
        a_shot[i].vel.x              = 0.f;
        a_shot[i].vel.y              = 0.f;
        a_shot[i].vel.z              = 0.f;
        a_shot[i].quat_orientation.x = 0.f;
        a_shot[i].quat_orientation.y = 0.f;
        a_shot[i].quat_orientation.z = 0.f;
        a_shot[i].quat_orientation.w = 1.f;
        a_shot[i].euler_rot.yaw      = 0.f;
        a_shot[i].euler_rot.pitch    = 0.f;
        a_shot[i].euler_rot.roll     = 0.f;
    }

    /*initialize asteroids*/
    a_aster = malloc(sizeof(A3DActor)*MAX_ASTEROIDS);
    for(i = 0; i < MAX_ASTEROIDS; i++)
    {
        a_aster[i].is_spawned         = false;
        a_aster[i].mass               = 0.f;
        a_aster[i].pos.x              = 0.f;
        a_aster[i].pos.y              = 0.f;
        a_aster[i].pos.z              = 0.f;
        a_aster[i].vel.x              = 0.f;
        a_aster[i].vel.y              = 0.f;
        a_aster[i].vel.z              = 0.f;
        a_aster[i].quat_orientation.x = 0.f;
        a_aster[i].quat_orientation.y = 0.f;
        a_aster[i].quat_orientation.z = 0.f;
        a_aster[i].quat_orientation.w = 1.f;
        a_aster[i].euler_rot.yaw      = 0.f;
        a_aster[i].euler_rot.pitch    = 0.f;
        a_aster[i].euler_rot.roll     = 0.f;
    }

    /*tie camera to player actor*/
    camera.player = &a_player;

    /*setup reticule*/
    strcpy(reticule[0].text, "\x0f");
    strcpy(reticule[1].text, "+");
    strcpy(reticule[2].text, "+");
    reticule[0].offset = 100.f;
    reticule[1].offset = 30.f;
    reticule[2].offset = 10.f;

    /*get base path name*/
    if(!(basepath = SDL_GetBasePath()))
    {
        fprintf(stderr, "Could not get executable base path.\n");
        return 1;
    }

    /*set model path and pointers for load_models*/
    m_player.file_root     = malloc(strlen(basepath) + 32);
    m_projectile.file_root = malloc(strlen(basepath) + 32);
    m_asteroid.file_root   = malloc(strlen(basepath) + 32);
    m_blast.file_root      = malloc(strlen(basepath) + 32);
    m_boundbox.file_root   = malloc(strlen(basepath) + 32);
    strcpy(m_player.file_root,     basepath);
    strcpy(m_projectile.file_root, basepath);
    strcpy(m_asteroid.file_root,   basepath);
    strcpy(m_blast.file_root,      basepath);
    strcpy(m_boundbox.file_root,   basepath);
    strcat(m_player.file_root,     "data/model/player1");
    strcat(m_projectile.file_root, "data/model/projectile1");
    strcat(m_asteroid.file_root,   "data/model/asteroid1");
    strcat(m_blast.file_root,      "data/model/blast2");
    strcat(m_boundbox.file_root,   "data/model/bounds1");
    m_ptr_all[0] = &m_player;
    m_ptr_all[1] = &m_projectile;
    m_ptr_all[2] = &m_asteroid;
    m_ptr_all[3] = &m_blast;
    m_ptr_all[4] = &m_boundbox;

    /*set image path*/
    i_font.filename = malloc(strlen(basepath) + 32);
    strcpy(i_font.filename, basepath);
    strcat(i_font.filename, "data/image/8x16s_bitfont.png");
    i_skybox.filename = malloc(strlen(basepath) + 32);
    strcpy(i_skybox.filename, basepath);
    strcat(i_skybox.filename, "data/image/skybox0d.png");

    /*free base path*/
    free(basepath);

    /*init*/
    SDL_Init(SDL_INIT_VIDEO);
    win_main = SDL_CreateWindow("Asteroids 3D", SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED, 800, 600, SDL_WINDOW_OPENGL);
    if(!win_main)
    {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    win_main_gl = SDL_GL_CreateContext(win_main);
    if(!win_main_gl)
    {
        fprintf(stderr, "SDL_GL_CreatContext failed: %s\n", SDL_GetError());
        return 1;
    }
    if(SDL_GL_SetSwapInterval(1))
    {
        fprintf(stderr, "SDL_GL_SetSwapInterval failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_GetDrawableSize(win_main, &width_real, &height_real);
    aspect_ratio = width_real/(float)height_real;
    if(SDL_SetRelativeMouseMode(SDL_TRUE))
        fprintf(stderr,
                "SDL_SetRelativeMouseMode failed. Mouse capture disabled.\n");
    /*fetch buffer object functions*/
    if(!SDL_GL_ExtensionSupported("GL_ARB_vertex_buffer_object"))
    {
        fprintf(stderr, "GL_ARB_vertex_buffer_object not supported\n");
        return 1;
    }
    if(!SDL_GL_ExtensionSupported("GL_ARB_pixel_buffer_object"))
    {
        fprintf(stderr, "GL_ARB_pixel_buffer_object not supported\n");
        return 1;
    }
    *(void **)(&glDeleteBuffersARB_ptr) =
        SDL_GL_GetProcAddress("glDeleteBuffersARB");
    *(void **)(&glGenBuffersARB_ptr) =
        SDL_GL_GetProcAddress("glGenBuffersARB");
    *(void **)(&glBindBufferARB_ptr) =
        SDL_GL_GetProcAddress("glBindBufferARB");
    *(void **)(&glBufferDataARB_ptr) =
        SDL_GL_GetProcAddress("glBufferDataARB");
    /*load models*/
    if(!load_models(m_ptr_all, 5))
        return 1;
    free(m_player.file_root);
    free(m_projectile.file_root);
    free(m_asteroid.file_root);
    free(m_blast.file_root);
    free(m_boundbox.file_root);
    /*load images*/
    i_font.data = stbi_load(i_font.filename, &i_font.width, &i_font.height,
                           &i_font.depth, 0);
    i_skybox.data = stbi_load(i_skybox.filename, &i_skybox.width,
                                &i_skybox.height, &i_skybox.depth, 1);
    if(i_font.data && i_font.depth == 1)
    {
        unsigned char *packed;
        unsigned pixbuffer;
        int tbytes;
        int bytes = (i_font.width * i_font.height)/8;
        packed = malloc(bytes);
        /*pack bitmapped font*/
        for(i = 0; i < bytes; i++)
        {
            packed[i] = 0x00;
            for(j = 0; j < 8; j++)
            {
                if(i_font.data[i*8+j] > 0) packed[i] |= 0x01;
                if(j < 7) packed[i] = packed[i] << 1;
            }
        }
        free(i_font.data);
        i_font.offset = 0;
        printf("Loaded image %s - %dx%d bitmap\n",
                i_font.filename, i_font.width, i_font.height);
        /*pack skybox textures*/
        tbytes = i_skybox.width * i_skybox.height;
        if(!i_skybox.data || i_skybox.depth != 1)
        {
            fprintf(stderr, "Could not process image file %s\n",
                    i_skybox.filename);
            perror("fopen error");
            return 1;
        }
        packed = realloc(packed, bytes + tbytes);
        memcpy(packed + bytes, i_skybox.data, tbytes);
        i_skybox.offset = bytes;
        bytes += tbytes;
        free(i_skybox.data);
        printf("Loaded image %s - %dx%dx%d texture\n",
                i_skybox.filename, i_skybox.width,
                i_skybox.height, i_skybox.depth);
        free(i_skybox.filename);
        /*send packed data to device memory*/
        glGenBuffersARB_ptr(1, &pixbuffer);
        glBindBufferARB_ptr(GL_PIXEL_UNPACK_BUFFER, pixbuffer);
        glBufferDataARB_ptr(GL_PIXEL_UNPACK_BUFFER, bytes, packed,
                            GL_STATIC_DRAW);
        free(packed);
        printf("Image data total: %d bytes\n\n", bytes);
    }
    else
    {
        fprintf(stderr, "Could not process image file %s\n", i_font.filename);
        perror("fopen error");
        return 1;
    }
    free(i_font.filename);
    /*setup*/
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    tmp_diffuse_color[0] = 0.5f;
    tmp_diffuse_color[1] = 0.5f;
    tmp_diffuse_color[2] = 0.5f;
    glMaterialfv(GL_FRONT, GL_SPECULAR, tmp_diffuse_color);
    glMateriali(GL_FRONT, GL_SHININESS, 127);
    glEnable(GL_RESCALE_NORMAL);
    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 500.f);
    glFogf(GL_FOG_END, 800.f);

    prevtime = SDL_GetTicks();

    /*spawn initial asteroids*/
    srand((unsigned)time(NULL));
    for(i = 0; i < INIT_ASTEROIDS; i++)
    {
        a_aster[i].is_spawned      = true;
        if(rand() & 0x01)      /*50%*/
            a_aster[i].mass        = ASTER_MED;
        else if(rand() & 0x01) /*25%*/
            a_aster[i].mass        = ASTER_LARGE;
        else                   /*25%*/
            a_aster[i].mass        = ASTER_SMALL;
        a_aster[i].pos.x           = (float)((rand()%500) - 250);
        a_aster[i].pos.y           = (float)((rand()%500) - 250);
        a_aster[i].pos.z           = ARENA_SIZE;
        a_aster[i].vel.x           = ((rand()%200) - 100) * 0.005f;
        a_aster[i].vel.y           = ((rand()%200) - 100) * 0.005f;
        a_aster[i].vel.z           = ((rand()%200) - 100) * 0.005f;
        a_aster[i].euler_rot.yaw   = ((rand()%400) - 200) * 0.0001f;
        a_aster[i].euler_rot.pitch = ((rand()%400) - 200) * 0.0001f;
        a_aster[i].euler_rot.roll  = ((rand()%400) - 200) * 0.0001f;
    }

    /*main loop*/
    while(!loop_exit)
    {
        do {
            /*get previous frame time*/
            currtime = SDL_GetTicks();
            if(frametime < 0.0001f)
            {
                frametime = (float)(currtime - prevtime);
                if(frametime > 250) frametime = 250;
            }
            /*min(frametime, target_time)*/
            if(frametime > target_time)
            {
                mintime = target_time;
                skip_dt = true;
            }
            else if(skip_dt)
            {
                skip_dt = false;
                if(frametime > target_time*0.2f)
                     mintime = frametime;
                else mintime = target_time;
            }
            else mintime = frametime;
        } while(frametime < 0.0001f);
        /*get time modifier*/
        timemod = mintime/target_time;
        difftime = currtime - prevtime;
        prevtime = currtime;

        /*events*/
        while(SDL_PollEvent(&ev_main))
        {
            if(ev_main.type == SDL_QUIT)
                loop_exit = true;
            else if(ev_main.type == SDL_KEYDOWN)
            {
                if(ev_main.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
                    loop_exit     = true;
                else if(ev_main.key.keysym.scancode == SDL_SCANCODE_BACKSPACE)
                {
                    if(camera.driftcam) camera.driftcam = false;
                    else                camera.driftcam = true;
                }
                else if(ev_main.key.keysym.scancode == SDL_SCANCODE_GRAVE)
                {
                    if(debug_level == 2) debug_level = 0;
                    else                 debug_level++;
                }
                else if(ev_main.key.keysym.scancode == SDL_SCANCODE_F1)
                {
                    int display;
                    SDL_DisplayMode d_mode;
                    if(fullscreen)
                    {
                        fullscreen = false;
                        SDL_SetWindowFullscreen(win_main, 0);
                        SDL_SetWindowSize(win_main, 800, 600);
                        SDL_GL_GetDrawableSize(win_main, &width_real,
                                              &height_real);
                    }
                    else
                    {
                        fullscreen = true;
                        display = SDL_GetWindowDisplayIndex(win_main);
                        SDL_GetDesktopDisplayMode(display, &d_mode);
                        SDL_SetWindowDisplayMode(win_main, &d_mode);
                        SDL_SetWindowFullscreen(win_main,
                                                SDL_WINDOW_FULLSCREEN);
                        SDL_GL_GetDrawableSize(win_main, &width_real,
                                              &height_real);
                    }
                }
                else if(ev_main.key.keysym.scancode == SDL_SCANCODE_W)
                    camera.forward  = true;
                else if(ev_main.key.keysym.scancode == SDL_SCANCODE_S)
                    camera.backward = true;
                else if(ev_main.key.keysym.scancode == SDL_SCANCODE_A)
                    camera.left     = true;
                else if(ev_main.key.keysym.scancode == SDL_SCANCODE_D)
                    camera.right    = true;
                else if(ev_main.key.keysym.scancode == SDL_SCANCODE_LSHIFT)
                    camera.up       = true;
                else if(ev_main.key.keysym.scancode == SDL_SCANCODE_LCTRL)
                    camera.down     = true;
                else if(ev_main.key.keysym.scancode == SDL_SCANCODE_Q)
                    camera.ccw      = true;
                else if(ev_main.key.keysym.scancode == SDL_SCANCODE_E)
                    camera.cw       = true;
            }
            else if(ev_main.type == SDL_KEYUP)
            {
                if(ev_main.key.keysym.scancode == SDL_SCANCODE_W)
                    camera.forward  = false;
                else if(ev_main.key.keysym.scancode == SDL_SCANCODE_S)
                    camera.backward = false;
                else if(ev_main.key.keysym.scancode == SDL_SCANCODE_A)
                    camera.left     = false;
                else if(ev_main.key.keysym.scancode == SDL_SCANCODE_D)
                    camera.right    = false;
                else if(ev_main.key.keysym.scancode == SDL_SCANCODE_LSHIFT)
                    camera.up       = false;
                else if(ev_main.key.keysym.scancode == SDL_SCANCODE_LCTRL)
                    camera.down     = false;
                else if(ev_main.key.keysym.scancode == SDL_SCANCODE_Q)
                    camera.ccw      = false;
                else if(ev_main.key.keysym.scancode == SDL_SCANCODE_E)
                    camera.cw       = false;
            }
            else if(ev_main.type == SDL_MOUSEMOTION)
            {
                a_player.euler_rot.yaw   = -camera.rotmod * camera.sens *
                                           (float)ev_main.motion.xrel;
                a_player.euler_rot.pitch = -camera.rotmod * camera.sens *
                                           (float)ev_main.motion.yrel;
            }
            else if(ev_main.type == SDL_MOUSEBUTTONDOWN)
            {
                if(ev_main.button.button == SDL_BUTTON_LEFT)
                    camera.shoot = true;
            }
            else if(ev_main.type == SDL_MOUSEBUTTONUP)
            {
                if(ev_main.button.button == SDL_BUTTON_LEFT)
                    camera.shoot = false;
            }
        }

        /*get view frustum values*/
        top_clip = tan(fov * camera.fovmod * radmod * 0.5f) * near_clip;
        bottom_clip = -top_clip;
        left_clip = aspect_ratio * bottom_clip;
        right_clip = -left_clip;

        /*update state*/
        if(camera.ccw)
           a_player.euler_rot.roll =  camera.rollmod * camera.rotmod * timemod;
        if(camera.cw)
           a_player.euler_rot.roll = -camera.rollmod * camera.rotmod * timemod;
        if(camera.shoot && a_player.is_spawned)
        {
            /*when button is pressed, or after 16 frames*/
            if(!shot_loop_count || currtime - shot_loop_count > 250)
            {
                shot_loop_count = currtime;
                /*find free projectile object*/
                for(i = 0; i < MAX_SHOTS; i++)
                {
                    if(a_shot[i].is_spawned)
                        continue;
                    a_shot[i].is_spawned = true;
                    a_shot[i].pos.x = -a_player.pos.x;
                    a_shot[i].pos.y = -a_player.pos.y;
                    a_shot[i].pos.z = -a_player.pos.z;
                    a_shot[i].vel.x = 0.f;
                    a_shot[i].vel.y = 0.f;
                    a_shot[i].vel.z = shot_speed;
                    /*180 degree yaw applied to conj(player)*/
                    a_shot[i].quat_orientation.x =-a_player.quat_orientation.z;
                    a_shot[i].quat_orientation.y = a_player.quat_orientation.w;
                    a_shot[i].quat_orientation.z = a_player.quat_orientation.x;
                    a_shot[i].quat_orientation.w = a_player.quat_orientation.y;
                    a_shot[i].euler_rot.yaw   = 0.f;
                    a_shot[i].euler_rot.pitch = 0.f;
                    a_shot[i].euler_rot.roll  = 0.f;
                    /*apply shot_speed to real vel vector*/
                    get_shot_vel(&(a_shot[i]));
                    /*add player's velocity*/
                    a_shot[i].vel.x -= a_player.vel.x;
                    a_shot[i].vel.y -= a_player.vel.y;
                    a_shot[i].vel.z -= a_player.vel.z;
                    break;
                }
            }
        }
        else
            shot_loop_count = 0;
        /*targeting reticules*/
        for(i = 0; i < 3; i++)
        {
            float *x = &a_player.quat_orientation.z,
                  *y = &a_player.quat_orientation.w,
                  *z = &a_player.quat_orientation.x,
                  *w = &a_player.quat_orientation.y;
            reticule[i].pos.x = -a_player.pos.x;
            reticule[i].pos.y = -a_player.pos.y;
            reticule[i].pos.z = -a_player.pos.z;
            reticule[i].pos.x += reticule[i].offset *
                (-2.f*(*x)*(*z) - 2.f*(*y)*(*w)) - a_player.vel.x;
            reticule[i].pos.y += reticule[i].offset *
                (2.f*(*y)*(*z) - 2.f*(*x)*(*w)) - a_player.vel.y;
            reticule[i].pos.z += reticule[i].offset *
                (1.f - 2.f*(*x)*(*x) - 2.f*(*y)*(*y)) - a_player.vel.z;
        }
        /*check asteroids*/
        for(i = 0; i < MAX_ASTEROIDS; i++)
        {
            float dx, dy, dz;
            if(!a_aster[i].is_spawned || !a_player.is_spawned)
                continue;
            /*player collision*/
            dx = a_aster[i].pos.x + a_player.pos.x;
            dy = a_aster[i].pos.y + a_player.pos.y;
            dz = a_aster[i].pos.z + a_player.pos.z;
            /*check collision*/
            if(inv_sqrt_dwh(dx*dx + dy*dy + dz*dz) > 0.8f/(a_aster[i].mass))
            {
                a_player.is_spawned     = false;
                blastmod                = 20.f;
                a_blast.is_spawned      = true;
                a_blast.mass            = 0.001f;
                a_blast.pos.x           = -a_player.pos.x;
                a_blast.pos.y           = -a_player.pos.y;
                a_blast.pos.z           = -a_player.pos.z;
                a_blast.euler_rot.yaw   = ((rand()%400) - 200) * 0.0001f;
                a_blast.euler_rot.pitch = ((rand()%400) - 200) * 0.0001f;
                a_blast.euler_rot.roll  = ((rand()%400) - 200) * 0.0001f;
            }
            /*projectile collision*/
            for(j = 0; j < MAX_SHOTS; j++)
            {
                if(!a_shot[j].is_spawned)
                    continue;
                /*get distance between shot and asteroid*/
                dx = a_shot[j].pos.x - a_aster[i].pos.x;
                dy = a_shot[j].pos.y - a_aster[i].pos.y;
                dz = a_shot[j].pos.z - a_aster[i].pos.z;
                /*check hit*/
                if(inv_sqrt_dwh(dx*dx + dy*dy + dz*dz) < 0.8f/a_aster[i].mass)
                    continue;
                a_shot[j].is_spawned = false;
                /*spawn scoretext object*/
                for(k = 0; k < 3; k++)
                {
                    if(scoretext[k].is_spawned)
                        continue;
                    scoretext[k].is_spawned = true;
                    scoretext[k].offset     = 0.f;
                    scoretext[k].pos.x      = a_aster[i].pos.x;
                    scoretext[k].pos.y      = a_aster[i].pos.y;
                    scoretext[k].pos.z      = a_aster[i].pos.z;
                    break;
                }
                /*spawn smaller asteroid*/
                if(a_aster[i].mass > (ASTER_LARGE + ASTER_MED)*0.5f)
                {
                    a_aster[i].mass = ASTER_MED;
                    score += 10;
                    if(k < 3) strcpy(scoretext[k].text, "+10");
                }
                else if(a_aster[i].mass > (ASTER_SMALL + ASTER_MED)*0.5f)
                {
                    a_aster[i].mass = ASTER_SMALL;
                    score += 20;
                    if(k < 3) strcpy(scoretext[k].text, "+20");
                }
                else
                {
                    a_aster[i].is_spawned = false;
                    score += 50;
                    if(k < 3) strcpy(scoretext[k].text, "+50");
                }
                a_aster[i].vel.x           = ((rand()%200) - 100) * 0.005f;
                a_aster[i].vel.y           = ((rand()%200) - 100) * 0.005f;
                a_aster[i].vel.z           = ((rand()%200) - 100) * 0.005f;
                a_aster[i].euler_rot.yaw   = ((rand()%400) - 200) * 0.0001f;
                a_aster[i].euler_rot.pitch = ((rand()%400) - 200) * 0.0001f;
                a_aster[i].euler_rot.roll  = ((rand()%400) - 200) * 0.0001f;
                /*spawn additional asteroid*/
                if(a_aster[i].is_spawned && (rand() & 0x01))
                {
                    for(k = 0; k < MAX_ASTEROIDS; k++)
                    {
                        if(a_aster[k].is_spawned)
                            continue;
                        a_aster[k].is_spawned     = true;
                        a_aster[k].mass           = ASTER_SMALL;
                        a_aster[k].pos.x          = a_aster[i].pos.x;
                        a_aster[k].pos.y          = a_aster[i].pos.y;
                        a_aster[k].pos.z          = a_aster[i].pos.z;
                        a_aster[k].vel.x          = ((rand()%200)-100)*0.005f;
                        a_aster[k].vel.y          = ((rand()%200)-100)*0.005f;
                        a_aster[k].vel.z          = ((rand()%200)-100)*0.005f;
                        a_aster[k].euler_rot.yaw  = ((rand()%400)-200)*0.0001f;
                        a_aster[k].euler_rot.pitch= ((rand()%400)-200)*0.0001f;
                        a_aster[k].euler_rot.roll = ((rand()%400)-200)*0.0001f;
                        break;
                    }
                }
            }
        }
        /*spawn new asteroid*/
        if(currtime - spawn_loop_count > 30000)
        {
            spawn_loop_count = currtime;
            for(i = 0; i < MAX_ASTEROIDS; i++)
            {
                if(a_aster[i].is_spawned)
                    continue;
                a_aster[i].is_spawned      = true;
                if(rand() & 0x01) /*50%*/
                    a_aster[i].mass        = ASTER_MED;
                else              /*50%*/
                    a_aster[i].mass        = ASTER_LARGE;
                a_aster[i].pos.x           = (float)((rand()%500) - 250);
                a_aster[i].pos.y           = (float)((rand()%500) - 250);
                a_aster[i].pos.z           = ARENA_SIZE;
                a_aster[i].vel.x           = ((rand()%200) - 100) * 0.005f;
                a_aster[i].vel.y           = ((rand()%200) - 100) * 0.005f;
                a_aster[i].vel.z           = ((rand()%200) - 100) * 0.005f;
                a_aster[i].euler_rot.yaw   = ((rand()%400) - 200) * 0.0001f;
                a_aster[i].euler_rot.pitch = ((rand()%400) - 200) * 0.0001f;
                a_aster[i].euler_rot.roll  = ((rand()%400) - 200) * 0.0001f;
                break;
            }
        }
        /*update scoretext offset*/
        for(i = 0; i < 3; i++)
        {
            if(!scoretext[i].is_spawned)
                continue;
            if(scoretext[i].offset > 1.f)
               scoretext[i].is_spawned = false;
            else
               scoretext[i].offset += 0.02f * timemod;
        }
        /*grow blast effect*/
        if(!a_player.is_spawned && a_blast.is_spawned)
        {
            if(a_blast.mass < 2.5f)
            {
                a_blast.mass += timemod/blastmod;
                camera.fovmod += 0.3f*timemod/blastmod;
                camera.pos_offset[2] -= 2.f*timemod/blastmod;
                blastmod += 0.5f*timemod;
            }
            else /*reset game*/
            {
                a_blast.is_spawned = false;
                camera.fovmod = 1.f;
                camera.pos_offset[2] = -5.f;
                /*reset score*/
                if(score > topscore) topscore = score;
                score = 0;
                reset_game(&a_player, a_aster);
            }
        }

        /*** drawing ***/
        glViewport(0, 0, width_real, height_real);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        /*bitmap text*/
        if(debug_level)
        {
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glOrtho(left_clip, right_clip, bottom_clip, top_clip, -1.f, 1.f);
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 128);
            glRasterPos3f(left_clip + 0.01f, bottom_clip + 0.02f, 0.f);
            for(i = 0; i < (signed)strlen(t_relvel); i++)   /*relative vel*/
                glBitmap(8, 16, 0.f, 0.f, 8.f, 0.f,
                        (void*)(intptr_t)(BITFONT_OFFSET(t_relvel[i])));
            glBitmap(8, 16, 0.f, 0.f, -8.f*(signed)strlen(t_relvel), 33.f,
                    (void*)(intptr_t)(BITFONT_OFFSET(' ')));
            for(i = 0; i < (signed)strlen(t_topscore); i++) /*top score*/
                glBitmap(8, 16, 0.f, 0.f, 8.f, 0.f,
                        (void*)(intptr_t)(BITFONT_OFFSET(t_topscore[i])));
            glBitmap(8, 16, 0.f, 0.f, -8.f*(signed)strlen(t_topscore), 17.f,
                    (void*)(intptr_t)(BITFONT_OFFSET(' ')));
            for(i = 0; i < (signed)strlen(t_score); i++)    /*score*/
                glBitmap(8, 16, 0.f, 0.f, 8.f, 0.f,
                        (void*)(intptr_t)(BITFONT_OFFSET(t_score[i])));
            if(debug_level > 1)
            {
                glBitmap(8, 16, 0.f, 0.f, -8.f*(signed)strlen(t_score), 33.f,
                        (void*)(intptr_t)(BITFONT_OFFSET(' ')));
                for(i = 0; i < (signed)strlen(t_mspf); i++) /*ms/F*/
                    glBitmap(8, 16, 0.f, 0.f, 8.f, 0.f,
                            (void*)(intptr_t)(BITFONT_OFFSET(t_mspf[i])));
                glBitmap(8, 16, 0.f, 0.f, -8.f*(signed)strlen(t_mspf), 17.f,
                        (void*)(intptr_t)(BITFONT_OFFSET(' ')));
                for(i = 0; i < (signed)strlen(t_fps); i++)  /*FPS*/
                    glBitmap(8, 16, 0.f, 0.f, 8.f, 0.f,
                            (void*)(intptr_t)(BITFONT_OFFSET(t_fps[i])));
            }
        }
        /*projection*/
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glFrustum(left_clip, right_clip, bottom_clip,
                  top_clip,  near_clip,  far_clip);
        /*modelview*/
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        /*player model*/
        glTranslatef(camera.pos_offset[0], camera.pos_offset[1],
                     camera.pos_offset[2]);
        glRotatef(camera.roll, 0.f, 0.f, 1.f);
        tmp_diffuse_color[0] = 1.f;
        tmp_diffuse_color[1] = 1.f;
        tmp_diffuse_color[2] = 1.f;
        glMaterialfv(GL_FRONT, GL_DIFFUSE, tmp_diffuse_color);
        if(a_player.is_spawned) draw_model(m_player);
        move_camera(&camera, timemod);
        draw_skybox(i_skybox, -a_player.pos.x, -a_player.pos.y,
                   -a_player.pos.z);
        /*blast*/
        if(!a_player.is_spawned)
        {
            glPushMatrix();
                glPushAttrib(GL_LIGHTING_BIT);
                tmp_diffuse_color[0] = 1.f;
                tmp_diffuse_color[1] = 1.f;
                tmp_diffuse_color[2] = 0.f;
                glMaterialfv(GL_FRONT, GL_SPECULAR, tmp_diffuse_color);
                tmp_diffuse_color[0] = 0.8f;
                tmp_diffuse_color[1] = 0.4f;
                tmp_diffuse_color[2] = 0.2f;
                glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE,
                             tmp_diffuse_color);
                transform_static_actor(&a_blast, timemod);
                glScalef(a_blast.mass, a_blast.mass, a_blast.mass);
                draw_model(m_blast);
                glPopAttrib();
            glPopMatrix();
        }
        /*** begin scene ***/
        /*bounding box*/
        glPushMatrix();
            glPushAttrib(GL_POLYGON_BIT|GL_ENABLE_BIT|
                         GL_FOG_BIT|GL_CURRENT_BIT);
            glDisable(GL_LIGHTING);
            glFogf(GL_FOG_START, 200.f);
            glFogf(GL_FOG_END, 300.f);
            glColor3f(0.8f, 0.f, 0.f);
            glPolygonMode(GL_FRONT, GL_LINE);
            glScalef(ARENA_SIZE, ARENA_SIZE, ARENA_SIZE);
            draw_model(m_boundbox);
            glPopAttrib();
        glPopMatrix();
        /*projectiles*/
        for(i = 0; i < MAX_SHOTS; i++)
        {
            float dx,dy,dz;
            if(!a_shot[i].is_spawned)
                continue;
            dx = a_shot[i].pos.x + a_player.pos.x;
            dy = a_shot[i].pos.y + a_player.pos.y;
            dz = a_shot[i].pos.z + a_player.pos.z;
            /*despawn shot if distance from player > 320*/
            if(inv_sqrt_dwh(dx*dx + dy*dy + dz*dz) < 0.003125f)
                a_shot[i].is_spawned = false;
            glPushMatrix();
                glPushAttrib(GL_LIGHTING_BIT);
                tmp_diffuse_color[0] = 0.f;
                tmp_diffuse_color[1] = 1.f;
                tmp_diffuse_color[2] = 1.f;
                glMaterialfv(GL_FRONT, GL_EMISSION, tmp_diffuse_color);
                transform_static_actor(&(a_shot[i]), timemod);
                draw_model(m_projectile);
                glPopAttrib();
            glPopMatrix();
        }
        /*asteroids*/
        for(i = 0; i < MAX_ASTEROIDS; i++)
        {
            if(!a_aster[i].is_spawned)
                continue;
            if(a_aster[i].mass > (ASTER_LARGE + ASTER_MED)*0.5f)
            {   /*more red*/
                tmp_diffuse_color[0] = 0.8f;
                tmp_diffuse_color[1] = 0.4f;
                tmp_diffuse_color[2] = 0.4f;
            }
            else if(a_aster[i].mass > (ASTER_SMALL + ASTER_MED)*0.5f)
            {   /*less red*/
                tmp_diffuse_color[0] = 0.8f;
                tmp_diffuse_color[1] = 0.6f;
                tmp_diffuse_color[2] = 0.6f;
            }
            else
            {   /*gray*/
                tmp_diffuse_color[0] = 0.8f;
                tmp_diffuse_color[1] = 0.8f;
                tmp_diffuse_color[2] = 0.8f;
            }
            glMaterialfv(GL_FRONT, GL_DIFFUSE, tmp_diffuse_color);
            glPushMatrix();
                transform_static_actor(&(a_aster[i]), timemod);
                glScalef(a_aster[i].mass, a_aster[i].mass, a_aster[i].mass);
                draw_model(m_asteroid);
            glPopMatrix();
        }
        /*scoretext objects*/
        for(i = 0; i < 3; i++)
        {
            if(!scoretext[i].is_spawned)
                continue;
            glPushAttrib(GL_CURRENT_BIT|GL_ENABLE_BIT);
            glDisable(GL_LIGHTING);
            glColor3f(0.5f - 0.5f*(scoretext[i].offset),
                      1.f - scoretext[i].offset, 0.f);
            glRasterPos3f(scoretext[i].pos.x, scoretext[i].pos.y,
                          scoretext[i].pos.z);
            for(j = 0; j < (signed)strlen(scoretext[i].text); j++)
                glBitmap(8, 16, 0.f, 0.f, 8.f, 0.f,
                    (void*)(intptr_t)(BITFONT_OFFSET(scoretext[i].text[j])));
            glPopAttrib();
        }
        /*targeting reticules*/
        for(i = 0; i < 3; i++)
        {
            glPushAttrib(GL_CURRENT_BIT|GL_ENABLE_BIT);
            glDisable(GL_LIGHTING);
            glDisable(GL_DEPTH_TEST);
            glColor3f(1.f, 1.f, 1.f);
            glRasterPos3f(reticule[i].pos.x, reticule[i].pos.y,
                          reticule[i].pos.z);
            glBitmap(8, 16, 0.f, 0.f, 8.f, 0.f,
                    (void*)(intptr_t)(BITFONT_OFFSET(reticule[i].text[0])));
            glPopAttrib();
        }
        /*** end scene ***/
        SDL_GL_SwapWindow(win_main);
        frametime -= mintime;
        /*update text/window title*/
        if(currtime - title_loop_count > 500)
        {
            float relvel = 16.f/(inv_sqrt_dwh(a_player.vel.x*a_player.vel.x +
                                              a_player.vel.y*a_player.vel.y +
                                              a_player.vel.z*a_player.vel.z));
            title_loop_count = currtime;
            sprintf(t_mspf,     "%u ms/F", difftime);
            sprintf(t_fps,      "%.2f FPS", 1000.f/(float)difftime);
            sprintf(t_relvel,   "Relative velocity: %.2f m/s", relvel);
            sprintf(t_score,    "Score:     %u", score);
            sprintf(t_topscore, "Top Score: %u", topscore);
            sprintf(win_title, "Asteroids 3D - %s - %s --- %s --- %s",
                    t_score, t_topscore, t_relvel, t_fps);
            SDL_SetWindowTitle(win_main, win_title);
        }
    }

    /*cleanup*/
    SDL_GL_DeleteContext(win_main_gl);
    SDL_DestroyWindow(win_main);
    SDL_Quit();
    return 0;
}

void reset_game(A3DActor *player, A3DActor *aster)
{
    int i;
    /*reset player*/
    player->is_spawned         = true;
    player->pos.x              = 0.f;
    player->pos.y              = 0.f;
    player->pos.z              = 0.f;
    player->vel.x              = 0.f;
    player->vel.y              = 0.f;
    player->vel.z              = 0.f;
    player->quat_orientation.x = 0.f;
    player->quat_orientation.y = 0.f;
    player->quat_orientation.z = 0.f;
    player->quat_orientation.w = 1.f;
    player->euler_rot.yaw      = 0.f;
    player->euler_rot.pitch    = 0.f;
    player->euler_rot.roll     = 0.f;
    /*reset asteroids*/
    for(i = 0; i < MAX_ASTEROIDS; i++)
    {
        aster[i].is_spawned      = false;
        if(i >= INIT_ASTEROIDS) continue;
        aster[i].is_spawned      = true;
        if(rand() & 0x01)      /*50%*/
            aster[i].mass        = ASTER_MED;
        else if(rand() & 0x01) /*25%*/
            aster[i].mass        = ASTER_LARGE;
        else                   /*25%*/
            aster[i].mass        = ASTER_SMALL;
        aster[i].pos.x           = (float)((rand()%500) - 250);
        aster[i].pos.y           = (float)((rand()%500) - 250);
        aster[i].pos.z           = ARENA_SIZE;
        aster[i].vel.x           = ((rand()%200) - 100) * 0.005f;
        aster[i].vel.y           = ((rand()%200) - 100) * 0.005f;
        aster[i].vel.z           = ((rand()%200) - 100) * 0.005f;
        aster[i].euler_rot.yaw   = ((rand()%400) - 200) * 0.0001f;
        aster[i].euler_rot.pitch = ((rand()%400) - 200) * 0.0001f;
        aster[i].euler_rot.roll  = ((rand()%400) - 200) * 0.0001f;
    }
}

void get_shot_vel(A3DActor *obj)
{
    float *x = &(obj->quat_orientation.x),
          *y = &(obj->quat_orientation.y),
          *z = &(obj->quat_orientation.z),
          *w = &(obj->quat_orientation.w);

    obj->vel.x = obj->vel.z * (2.f*(*x)*(*z) - 2.f*(*y)*(*w));
    obj->vel.y = obj->vel.z * (2.f*(*y)*(*z) + 2.f*(*x)*(*w));
    obj->vel.z = obj->vel.z * (1.f - 2.f*(*x)*(*x) - 2.f*(*y)*(*y));
}

void rotate_static_actor(A3DActor *obj, float *m, float dt)
{
    float tmp, x2, y2, z2, w2,
          s1, s2, s3, c1, c2, c3;
    float *x = &(obj->quat_orientation.x),
          *y = &(obj->quat_orientation.y),
          *z = &(obj->quat_orientation.z),
          *w = &(obj->quat_orientation.w);

    /*euler -> quat*/
    s1 = sin(obj->euler_rot.yaw   * 0.5f * dt);
    s2 = sin(obj->euler_rot.roll  * 0.5f * dt);
    s3 = sin(obj->euler_rot.pitch * 0.5f * dt);
    c1 = cos(obj->euler_rot.yaw   * 0.5f * dt);
    c2 = cos(obj->euler_rot.roll  * 0.5f * dt);
    c3 = cos(obj->euler_rot.pitch * 0.5f * dt);
    w2 = c1*c2*c3 - s1*s2*s3;
    x2 = s1*s2*c3 + c1*c2*s3;
    y2 = s1*c2*c3 + c1*s2*s3;
    z2 = c1*s2*c3 - s1*c2*s3;
    /*normalize*/
    tmp = x2*x2 + y2*y2 + z2*z2 + w2*w2;
    if(fabs(tmp - 1.f) > SQRT_TOLERANCE)
    {
        if(tmp > SQRT_TOLERANCE)
        {
            tmp = inv_sqrt_dwh(tmp);
            x2 *= tmp;
            y2 *= tmp;
            z2 *= tmp;
            w2 *= tmp;
        }
        else /*identity*/
        {
            w2 = 1.f;
            x2 = 0.f;
            y2 = 0.f;
            z2 = 0.f;
        }
    }

    /*multiply quats*/
    s1 = *w*x2 + *x*w2 + *y*z2 - *z*y2;
    s2 = *w*y2 + *y*w2 + *z*x2 - *x*z2;
    s3 = *w*z2 + *z*w2 + *x*y2 - *y*x2;
    *w = *w*w2 - *x*x2 - *y*y2 - *z*z2;
    *x = s1;
    *y = s2;
    *z = s3;

    /*quat -> transposed rotation matrix
     *
     * | x(x) y(x) z(x) tx |
     * | x(y) y(y) z(y) ty |
     * | x(z) y(z) z(z) tz |
     * |  0    0    0    1 |
     *
     * where x() is the x axis direction,
     * y() is the y axis direction, and
     * z() is the z axis direction. Each
     * axis has a (x,y,z) component.
     */
    m[0] = 1.f - 2.f*(*y)*(*y) - 2.f*(*z)*(*z);
    m[1] = 2.f*(*x)*(*y) - 2.f*(*z)*(*w);
    m[2] = 2.f*(*x)*(*z) + 2.f*(*y)*(*w);
    m[3] = 0.f;

    m[4] = 2.f*(*x)*(*y) + 2.f*(*z)*(*w);
    m[5] = 1.f - 2.f*(*x)*(*x) - 2.f*(*z)*(*z);
    m[6] = 2.f*(*y)*(*z) - 2.f*(*x)*(*w);
    m[7] = 0.f;

    m[8] = 2.f*(*x)*(*z) - 2.f*(*y)*(*w);
    m[9] = 2.f*(*y)*(*z) + 2.f*(*x)*(*w);
    m[10] = 1.f - 2.f*(*x)*(*x) - 2.f*(*y)*(*y);
    m[11] = 0.f;
}

void translate_static_actor(A3DActor *obj, float *m, float dt)
{
    float *x = &(obj->pos.x),
          *y = &(obj->pos.y),
          *z = &(obj->pos.z);

    /*move along vel vector*/
    *x += obj->vel.x * dt;
    *y += obj->vel.y * dt;
    *z += obj->vel.z * dt;
    /*wrap position*/
    if(*x >  ARENA_SIZE)
       *x = -ARENA_SIZE + 0.001f;
    if(*x < -ARENA_SIZE)
       *x =  ARENA_SIZE - 0.001f;
    if(*y >  ARENA_SIZE)
       *y = -ARENA_SIZE + 0.001f;
    if(*y < -ARENA_SIZE)
       *y =  ARENA_SIZE - 0.001f;
    if(*z >  ARENA_SIZE)
       *z = -ARENA_SIZE + 0.001f;
    if(*z < -ARENA_SIZE)
       *z =  ARENA_SIZE - 0.001f;

    /*update translation component of matrix*/
    m[12] = *x;
    m[13] = *y;
    m[14] = *z;
    m[15] = 1.f;
}

void transform_static_actor(A3DActor *obj, float dt)
{
    float m[16];
    rotate_static_actor(obj, m, dt);
    translate_static_actor(obj, m, dt);
    glMultMatrixf(m);
}

void move_camera(A3DCamera *cam, float dt)
{
    static float zz = 0.02f, yacc, pacc;
    float s1, s2, s3, m[16];
    float *x = &(cam->player->pos.x),
          *y = &(cam->player->pos.y),
          *z = &(cam->player->pos.z);

    /*camera movement panning/zooming*/
    if(fabs(cam->player->euler_rot.yaw) < 0.000001f)
    {
        if(yacc < 1000.f) yacc += dt;
    }
    else yacc = 0.f;
    if(fabs(cam->player->euler_rot.pitch) < 0.000001f)
    {
        if(pacc < 1000.f) pacc += dt;
    }
    else pacc = 0.f;
    if(cam->driftcam)
    {
        cam->roll += cam->player->euler_rot.yaw * 0.5f * dt/radmod;
        cam->pos_offset[1] -= cam->player->euler_rot.pitch * 0.02f * dt/radmod;
    }
    if(yacc > 10.f || !cam->driftcam)
    {
        if(cam->roll < -1.f)     cam->roll += 0.5f*dt;
        else if(cam->roll > 1.f) cam->roll -= 0.5f*dt;
        else                     cam->roll = 0.f;
    }
    if(pacc > 10.f || !cam->driftcam)
    {
        if(cam->pos_offset[1] < -2.05f)      cam->pos_offset[1] += 0.02f*dt;
        else if(cam->pos_offset[1] > -1.95f) cam->pos_offset[1] -= 0.02f*dt;
        else                                 cam->pos_offset[1] = -2.f;
    }
    cam->pos_offset[0] = 0.1f * cam->roll;
    if(cam->roll > 15.f)
       cam->roll = 15.f;
    if(cam->roll < -15.f)
       cam->roll = -15.f;
    if(cam->pos_offset[1] < -3.f)
       cam->pos_offset[1] = -3.f;
    if(cam->pos_offset[1] > -1.f)
       cam->pos_offset[1] = -1.f;

    /*update rotation*/
    rotate_static_actor(cam->player, m, dt);
    /*stop applying rotation*/
    cam->player->euler_rot.yaw   = 0.f;
    cam->player->euler_rot.roll  = 0.f;
    cam->player->euler_rot.pitch = 0.f;

    if(cam->player->is_spawned)
    {
        /*increment velocity*/
        if(cam->forward ^ cam->backward) /*along z axis*/
        {
            if(zz > 0.005f) zz -= 0.001f*dt;
            s1 = m[2]  * cam->velmod * dt;
            s2 = m[6]  * cam->velmod * dt;
            s3 = m[10] * cam->velmod * dt;
            if(cam->forward) /*forward*/
            {
                cam->player->vel.x += s1;
                cam->player->vel.y += s2;
                cam->player->vel.z += s3;
                if(cam->fovmod < 1.2f && cam->driftcam)
                    cam->fovmod += dt*zz;
            }
            else               /*backward*/
            {
                cam->player->vel.x -= s1;
                cam->player->vel.y -= s2;
                cam->player->vel.z -= s3;
                if(cam->fovmod > 0.8f && cam->driftcam)
                    cam->fovmod -= dt*zz;
            }
        }
        else
        {
            zz = 0.02f;
            if(cam->fovmod > 1.02f)
                 cam->fovmod -= 1.5f*dt*zz;
            else if(cam->fovmod < 0.98f)
                 cam->fovmod += 1.5f*dt*zz;
            else cam->fovmod = 1.f;
        }
        if(cam->left ^ cam->right) /*along x axis*/
        {
            s1 = m[0] * cam->velmod * dt;
            s2 = m[4] * cam->velmod * dt;
            s3 = m[8] * cam->velmod * dt;
            if(cam->left) /*left*/
            {
                cam->player->vel.x += s1;
                cam->player->vel.y += s2;
                cam->player->vel.z += s3;
            }
            else            /*right*/
            {
                cam->player->vel.x -= s1;
                cam->player->vel.y -= s2;
                cam->player->vel.z -= s3;
            }
        }
        if(cam->up ^ cam->down) /*along y axis*/
        {
            s1 = m[1] * cam->velmod * dt;
            s2 = m[5] * cam->velmod * dt;
            s3 = m[9] * cam->velmod * dt;
            if(cam->up) /*up*/
            {
                cam->player->vel.x -= s1;
                cam->player->vel.y -= s2;
                cam->player->vel.z -= s3;
            }
            else          /*down*/
            {
                cam->player->vel.x += s1;
                cam->player->vel.y += s2;
                cam->player->vel.z += s3;
            }
        }
        *x += cam->player->vel.x * dt;
        *y += cam->player->vel.y * dt;
        *z += cam->player->vel.z * dt;
        /*wrap position*/
        if(*x >  ARENA_SIZE)
           *x = -ARENA_SIZE + 0.001f;
        if(*x < -ARENA_SIZE)
           *x =  ARENA_SIZE - 0.001f;
        if(*y >  ARENA_SIZE)
           *y = -ARENA_SIZE + 0.001f;
        if(*y < -ARENA_SIZE)
           *y =  ARENA_SIZE - 0.001f;
        if(*z >  ARENA_SIZE)
           *z = -ARENA_SIZE + 0.001f;
        if(*z < -ARENA_SIZE)
           *z =  ARENA_SIZE - 0.001f;
    }

    /*update translation component of matrix*/
    m[12] = m[0]*(*x) + m[4]*(*y) + m[8]*(*z);
    m[13] = m[1]*(*x) + m[5]*(*y) + m[9]*(*z);
    m[14] = m[2]*(*x) + m[6]*(*y) + m[10]*(*z);
    m[15] = 1.f;

    glMultMatrixf(m);
}

float inv_sqrt_dwh(float x)
{
    float const mx = 0.5f*1.000876311302185f*x;
    int xi         = *(int*)&x;
    xi             = 0x5f375a87 - (xi >> 1);
    x              = *(float*)&xi;
    return x*(1.5f*1.000876311302185f - mx*x*x);
}

bool load_model_from_file(const char *file_prefix, A3DModel *model)
{
    FILE *file_data;
    char metadata_line[256] = {'\0'};
    char *nl;
    char *token_buffer;
    char *file_path;
    char ixsum[16]  = {'\0'}; /*read from *.met*/
    char nvsum[16]  = {'\0'};
    char ixsum_[16] = {'\0'}; /*computed from index/vertex data*/
    char nvsum_[16] = {'\0'};
    unsigned ix_checksum = 0;
    unsigned nv_checksum = 0;
    unsigned tmp_uint    = 0;
    int i = 0;

    /*allocate apropriate space for path*/
    file_path = malloc(strlen(file_prefix) + 5);
    /*get metadata*/
    strcpy(file_path, file_prefix);
    strcat(file_path, ".met");
    if((file_data = fopen(file_path, "r")) == NULL)
    {
        fprintf(stderr, "Could not open: %s\n", file_path);
        perror("fopen error");
        free(file_path);
        return false;
    }
    while(!feof(file_data))
    {
        if(!fgets(metadata_line, sizeof(metadata_line), file_data))
            break;
        nl = strchr(metadata_line, '\n');
        if(nl)
            *nl = '\0';
        token_buffer = strtok(metadata_line, " ");
        if(!token_buffer)
            continue;
        /*get index count*/
        if(!strcmp(token_buffer, "indexcount:"))
        {
            if((token_buffer = strtok(NULL, " ")))
            {
                model->index_count = atoi(token_buffer);
                if(!model->index_count)
                {
                    fprintf(stderr,
                      "atoi error: Could not load index count from file %s\n",
                      file_path);
                    fclose(file_data);
                    free(file_path);
                    return false;
                }
            }
        }
        /*get vertex count*/
        else if(!strcmp(token_buffer, "vertexcount:"))
        {
            if((token_buffer = strtok(NULL, " ")))
            {
                model->vertex_count = atoi(token_buffer);
                if(!model->vertex_count)
                {
                    fprintf(stderr,
                      "atoi error: Could not load vertex count from file %s\n",
                      file_path);
                    fclose(file_data);
                    free(file_path);
                    return false;
                }
            }
        }
        /*get index/vertex checksum*/
        else if(!strcmp(token_buffer, "indexsum:"))
        {
            size_t ixsum_len = strlen(token_buffer);
            if(ixsum_len > 15)
                ixsum_len = 15;
            if((token_buffer = strtok(NULL, " ")))
                memcpy(ixsum, token_buffer, ixsum_len);
        }
        else if(!strcmp(token_buffer, "vertexsum:"))
        {
            size_t nvsum_len = strlen(token_buffer);
            if(nvsum_len > 15)
                nvsum_len = 15;
            if((token_buffer = strtok(NULL, " ")))
                memcpy(nvsum, token_buffer, nvsum_len);
        }
    }
    fclose(file_data);

    /*get index data*/
    model->index_data = malloc(model->index_count*sizeof(unsigned));
    strcpy(file_path, file_prefix);
    strcat(file_path, ".ix");
    if((file_data = fopen(file_path, "rb")) == NULL)
    {
        perror("fopen error");
        free(file_path);
        return false;
    }
    if(fread(model->index_data, sizeof(unsigned), model->index_count,
                file_data) != (unsigned)model->index_count)
    {
        fprintf(stderr,
                "fread error: Could not read %lu bytes of data from file %s\n",
                model->index_count*sizeof(unsigned), file_path);
        fclose(file_data);
        free(file_path);
        return false;
    }
    fclose(file_data);

    /*get vertex data*/
    model->vertex_data = malloc(model->vertex_count*sizeof(float));
    strcpy(file_path, file_prefix);
    strcat(file_path, ".nv");
    if((file_data = fopen(file_path, "rb")) == NULL)
    {
        perror("fopen error");
        free(file_path);
        return false;
    }
    if(fread(model->vertex_data, sizeof(float), model->vertex_count,
                file_data) != (unsigned)model->vertex_count)
    {
        fprintf(stderr,
                "fread error: Could not read %lu bytes of data from file %s\n",
                model->vertex_count*sizeof(float), file_path);
        fclose(file_data);
        free(file_path);
        return false;
    }
    fclose(file_data);

    /*check checksums*/
    for(i = 0; i < (model->index_count - 1); i += 64)
    {
        ix_checksum = ix_checksum ^ model->index_data[i];
        ix_checksum = ix_checksum ^ (model->index_data[i+1] >> 16);
        ix_checksum = ix_checksum ^ (model->index_data[i+1] << 16);
    }
    for(i = 0; i < model->vertex_count; i += 64)
    {
        memcpy(&tmp_uint, &model->vertex_data[i], sizeof(float));
        nv_checksum = nv_checksum ^ tmp_uint;
    }
    sprintf(ixsum_, "%x", ix_checksum);
    sprintf(nvsum_, "%x", nv_checksum);
    if(strcmp(ixsum_,ixsum))
    {
        fprintf(stderr, "Index checksum mismatch. Got %s instead of %s.\n",
                ixsum_, ixsum);
        free(file_path);
        return false;
    }
    if(strcmp(nvsum_,nvsum))
    {
        fprintf(stderr, "Vertex checksum mismatch. Got %s instead of %s.\n",
                nvsum_, nvsum);
        free(file_path);
        return false;
    }

    printf("Loaded model %s - %d indices - %d vertices\n", file_prefix,
            model->index_count, model->vertex_count);
    free(file_path);
    return true;
}

bool load_models(A3DModel **model, const int count)
{
    int i;
    unsigned  tmp_icount = 0;
    unsigned  tmp_vcount = 0;
    unsigned  all_icount = 0;
    unsigned  all_vcount = 0;
    unsigned *all_idata;
    float    *all_vdata;

    /*clear buffers of any previous call to load_models*/
    static unsigned buffer[2];
    if(buffer[0] || buffer[1])
        glDeleteBuffersARB_ptr(2, buffer);

    /*load models from file*/
    for(i = 0; i < count; i++)
    {
        if(!load_model_from_file(model[i]->file_root, model[i]))
        {
            fprintf(stderr, "Failed to load model from file\n");
            return false;
        }
        /*get total number of elements*/
        all_vcount += model[i]->vertex_count;
        all_icount += model[i]->index_count;
        /*get offsets*/
        if(!i)
        {
            model[0]->vertex_offset = 0;
            model[0]->index_offset  = 0;
            continue;
        }
        model[i]->vertex_offset =
            model[i-1]->vertex_offset +
            model[i-1]->vertex_count*sizeof(float);
        model[i]->index_offset =
            model[i-1]->index_offset +
            model[i-1]->index_count*sizeof(unsigned);
    }

    /*build combined arrays*/
    all_vdata = malloc(sizeof(float) * all_vcount);
    all_idata = malloc(sizeof(unsigned) * all_icount);
    for(i = 0; i < count; i++)
    {
        memcpy(all_vdata + tmp_vcount,
                model[i]->vertex_data,
                model[i]->vertex_count * sizeof(float));
        memcpy(all_idata + tmp_icount,
                model[i]->index_data,
                model[i]->index_count * sizeof(unsigned));
        tmp_vcount += model[i]->vertex_count;
        tmp_icount += model[i]->index_count;
        /*free model data*/
        free(model[i]->vertex_data);
        free(model[i]->index_data);
    }

    /*copy index/vertex data to device memory*/
    glGenBuffersARB_ptr(2, buffer);
    glBindBufferARB_ptr(GL_ARRAY_BUFFER, buffer[0]);
    glBufferDataARB_ptr(GL_ARRAY_BUFFER,
                        sizeof(float) * all_vcount,
                        all_vdata, GL_STATIC_DRAW);
    glBindBufferARB_ptr(GL_ELEMENT_ARRAY_BUFFER, buffer[1]);
    glBufferDataARB_ptr(GL_ELEMENT_ARRAY_BUFFER,
                        sizeof(unsigned) * all_icount,
                        all_idata, GL_STATIC_DRAW);
    glInterleavedArrays(GL_N3F_V3F, 0, (void*)(intptr_t)(0));

    /*free index/vertex data*/
    free(all_idata);
    free(all_vdata);

    printf("Model data total: %lu bytes\n\n", sizeof(float) * all_vcount +
                                              sizeof(unsigned) * all_icount);
    return true;
}

void draw_model(const A3DModel model)
{
    glInterleavedArrays(GL_N3F_V3F,0,(void*)(intptr_t)model.vertex_offset);
    glDrawElements(GL_TRIANGLES, model.index_count, GL_UNSIGNED_INT,
            (void*)(intptr_t)model.index_offset);
}

void draw_skybox(const A3DImage skybox,
                 const float x, const float y, const float z)
{
    glPushAttrib(GL_ENABLE_BIT|GL_DEPTH_BUFFER_BIT);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glEnable(GL_TEXTURE_2D);
    glDepthMask(GL_FALSE);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, skybox.depth, skybox.width,
            skybox.height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
            (void*)(intptr_t)skybox.offset);
    glPushMatrix();
    glTranslatef(x, y, z - 100.f);
    glBegin(GL_QUADS);
    glTexCoord2f(1.f, 1.f);
    glVertex3f(100.f, 100.f, 0.f);
    glTexCoord2f(0.f, 1.f);
    glVertex3f(-100.f, 100.f, 0.f);
    glTexCoord2f(0.f, 0.f);
    glVertex3f(-100.f, -100.f, 0.f);
    glTexCoord2f(1.f, 0.f);
    glVertex3f(100.f, -100.f, 0.f);
    glEnd();
    glPopMatrix();
    glTexImage2D(GL_TEXTURE_2D, 0, skybox.depth, skybox.width,
            skybox.height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
            (void*)(intptr_t)skybox.offset);
    glPushMatrix();
    glTranslatef(x + 100.f, y, z);
    glBegin(GL_QUADS);
    glTexCoord2f(1.f, 1.f);
    glVertex3f(0.f, 100.f, 100.f);
    glTexCoord2f(0.f, 1.f);
    glVertex3f(0.f, 100.f, -100.f);
    glTexCoord2f(0.f, 0.f);
    glVertex3f(0.f, -100.f, -100.f);
    glTexCoord2f(1.f, 0.f);
    glVertex3f(0.f, -100.f, 100.f);
    glEnd();
    glPopMatrix();
    glTexImage2D(GL_TEXTURE_2D, 0, skybox.depth, skybox.width,
            skybox.height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
            (void*)(intptr_t)skybox.offset);
    glPushMatrix();
    glTranslatef(x, y, z + 100.f);
    glBegin(GL_QUADS);
    glTexCoord2f(1.f, 1.f);
    glVertex3f(-100.f, 100.f, 0.f);
    glTexCoord2f(0.f, 1.f);
    glVertex3f(100.f, 100.f, 0.f);
    glTexCoord2f(0.f, 0.f);
    glVertex3f(100.f, -100.f, 0.f);
    glTexCoord2f(1.f, 0.f);
    glVertex3f(-100.f, -100.f, 0.f);
    glEnd();
    glPopMatrix();
    glTexImage2D(GL_TEXTURE_2D, 0, skybox.depth, skybox.width,
            skybox.height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
            (void*)(intptr_t)skybox.offset);
    glPushMatrix();
    glTranslatef(x - 100.f, y, z);
    glBegin(GL_QUADS);
    glTexCoord2f(1.f, 1.f);
    glVertex3f(0.f, 100.f, -100.f);
    glTexCoord2f(0.f, 1.f);
    glVertex3f(0.f, 100.f, 100.f);
    glTexCoord2f(0.f, 0.f);
    glVertex3f(0.f, -100.f, 100.f);
    glTexCoord2f(1.f, 0.f);
    glVertex3f(0.f, -100.f, -100.f);
    glEnd();
    glPopMatrix();
    glTexImage2D(GL_TEXTURE_2D, 0, skybox.depth, skybox.width,
            skybox.height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
            (void*)(intptr_t)skybox.offset);
    glPushMatrix();
    glTranslatef(x, y - 100.f, z);
    glBegin(GL_QUADS);
    glTexCoord2f(1.f, 1.f);
    glVertex3f(-100.f, 0.f, 100.f);
    glTexCoord2f(0.f, 1.f);
    glVertex3f(100.f, 0.f, 100.f);
    glTexCoord2f(0.f, 0.f);
    glVertex3f(100.f, 0.f, -100.f);
    glTexCoord2f(1.f, 0.f);
    glVertex3f(-100.f, 0.f, -100.f);
    glEnd();
    glPopMatrix();
    glTexImage2D(GL_TEXTURE_2D, 0, skybox.depth, skybox.width,
            skybox.height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
            (void*)(intptr_t)skybox.offset);
    glPushMatrix();
    glTranslatef(x, y + 100.f, z);
    glBegin(GL_QUADS);
    glTexCoord2f(1.f, 1.f);
    glVertex3f(-100.f, 0.f, -100.f);
    glTexCoord2f(0.f, 1.f);
    glVertex3f(100.f, 0.f, -100.f);
    glTexCoord2f(0.f, 0.f);
    glVertex3f(100.f, 0.f, 100.f);
    glTexCoord2f(1.f, 0.f);
    glVertex3f(-100.f, 0.f, 100.f);
    glEnd();
    glPopMatrix();
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 128);
    glPopAttrib();
}
