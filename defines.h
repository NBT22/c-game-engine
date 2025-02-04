//
// Created by droc101 on 4/20/2024.
//

#ifndef GAME_DEFINES_H
#define GAME_DEFINES_H

#include <box2d/id.h>
#include <box2d/math_functions.h>
#include <SDL.h>
#include <SDL_mixer.h>
#include <stdbool.h>
#include <stdint.h>
#include "config.h"
#include "Helpers/Core/List.h"

#pragma region Forward Declarations/Typedefs

// Basic types
typedef uint8_t byte;
typedef uint16_t ushort;
typedef uint32_t uint;
typedef uint64_t ulong;

// Enum forward declarations
typedef enum CurrentState CurrentState;
typedef enum Renderer Renderer;
typedef enum OptionsMsaa OptionsMsaa;
typedef enum ModelShader ModelShader;
typedef enum ImageDataOffsets ImageDataOffsets;
typedef enum AssetType AssetType;
typedef enum TriggerFlag TriggerFlag;

// Struct forward declarations
typedef struct GlobalState GlobalState;
typedef b2Vec2 Vector2;
typedef struct Camera Camera;
typedef struct Player Player;
typedef struct Wall Wall;
typedef struct Level Level;
typedef struct RayCastResult RayCastResult;
typedef struct TextBox TextBox;
typedef struct Model Model;
typedef struct Actor Actor;
typedef struct ModelHeader ModelHeader;
typedef struct Options Options;
typedef struct Asset Asset;
typedef struct Image Image;
typedef struct Trigger Trigger;
typedef struct Font Font;

// Function signatures
typedef void (*FixedUpdateFunction)(GlobalState *state, double delta);

typedef void (*FrameUpdateFunction)(GlobalState *state);

typedef void (*FrameRenderFunction)(GlobalState *state);

typedef void (*TextBoxCloseFunction)(TextBox *textBox);

typedef void (*ActorInitFunction)(Actor *self, b2WorldId worldId);

typedef void (*ActorUpdateFunction)(Actor *self, double delta);

typedef void (*ActorDestroyFunction)(Actor *self);

typedef void (*ActorSignalHandlerFunction)(Actor *self, const Actor *sender, int signal);

#pragma endregion

#pragma region Utility defines

#define STR(x) #x
#define TO_STR(x) STR(x)
#define PHYSICS_TARGET_MS (1000 / PHYSICS_TARGET_TPS)
#define PHYSICS_TARGET_MS_D (1000.0 / PHYSICS_TARGET_TPS)
#define PHYSICS_TARGET_NS (1000000000 / PHYSICS_TARGET_TPS) // nanoseconds because precision
#define PHYSICS_TARGET_NS_D (1000000000.0 / PHYSICS_TARGET_TPS)

/// Use this for the "OK/Accept" button in place of hardcoding controller A or B buttons
#define CONTROLLER_OK (GetState()->options.controllerSwapOkCancel ? SDL_CONTROLLER_BUTTON_B : SDL_CONTROLLER_BUTTON_A)
/// Use this for the "Cancel" button in place of hardcoding controller A or B buttons
#define CONTROLLER_CANCEL \
	(GetState()->options.controllerSwapOkCancel ? SDL_CONTROLLER_BUTTON_A : SDL_CONTROLLER_BUTTON_B)

#ifdef WIN32
/// Make this symbol exported (in the symbol table)
#define EXPORT_SYM __declspec(dllexport)
#else
/// Make this symbol exported (in the symbol table)
#define EXPORT_SYM __attribute__((visibility("default")))
#endif

#pragma endregion

#pragma region Enum definitions

enum AssetType
{
	ASSET_TYPE_TEXTURE = 0,
	ASSET_TYPE_MP3 = 1,
	ASSET_TYPE_WAV = 2,
	ASSET_TYPE_LEVEL = 3,
	ASSET_TYPE_GLSL = 4,
	ASSET_TYPE_SPIRV_FRAG = 5,
	ASSET_TYPE_SPIRV_VERT = 6,
	ASSET_TYPE_MODEL = 7,
	ASSET_TYPE_FONT = 8
};

/**
 * Use to get data from a decompressed image asset using @c ReadUintA
 */
enum ImageDataOffsets
{
	IMAGE_SIZE_OFFSET = 0,
	IMAGE_WIDTH_OFFSET = 4,
	IMAGE_HEIGHT_OFFSET = 8,
	IMAGE_ID_OFFSET = 12
};

/**
 * Used to check which game state the game is in
 * Now, you *could* just set a complete mess of state functions and disregard this, but if you do that, I will find you.
 */
enum CurrentState
{
	LEVEL_SELECT_STATE,
	LOGO_SPLASH_STATE,
	MAIN_STATE,
	MENU_STATE,
	PAUSE_STATE,
	OPTIONS_STATE,
	VIDEO_OPTIONS_STATE,
	SOUND_OPTIONS_STATE,
	INPUT_OPTIONS_STATE,
	LOADING_STATE,
};

/**
 * Used to check which renderer the game is using
 */
enum Renderer
{
	RENDERER_OPENGL,
	RENDERER_VULKAN,
	RENDERER_MAX
};

/**
 * Used the check the MSAA level setting
 */
enum OptionsMsaa
{
	MSAA_NONE = 0,
	MSAA_2X = 1,
	MSAA_4X = 2,
	MSAA_8X = 3
};

/**
 * List of shaders a model can be rendered with
 */
enum ModelShader
{
	/// The sky shader. Do not use on in-level models.
	SHADER_SKY,
	/// A basic shader with no lighting
	SHADER_UNSHADED,
	/// A shader with basic lighting based on the vertex normals.
	SHADER_SHADED
};

/**
 * List of flags that can be set on a trigger
 */
enum TriggerFlag
{
	/// The trigger will be removed after it is triggered
	TRIGGER_FLAG_ONE_SHOT = 1
};

#pragma endregion

#pragma region Struct definitions

struct Camera
{
	/// The X position of the camera
	float x;
	/// The Y position of the camera
	float y;
	/// The Z position of the camera
	float z;

	/// The pitch of the camera
	float pitch;
	/// The yaw of the camera
	float yaw;
	/// The roll of the camera
	float roll;

	/// The field of view of the camera
	float fov;
};

struct Player
{
	/// The player's position
	Vector2 pos;
	/// The player's rotation
	double angle;
	/// The player's box2d body ID
	b2BodyId bodyId;
};

// Utility functions are in Structs/wall.h
struct Wall
{
	/// The first point of the wall
	Vector2 a;
	/// The second point of the wall
	Vector2 b;
	/// The fully qualified texture name (texture/level_uvtest.gtex instead of level_uvtest)
	const char tex[48];
	/// The length of the wall (Call @c WallBake to update)
	float length;
	/// The angle of the wall (Call @c WallBake to update)
	float angle;
	/// The change in x over the length of the wall, calculated with @code Wall.b.x - Wall.a.x@endcode
	double dx;
	/// The change in y over the length of the wall, calculated with @code Wall.b.y - Wall.a.y@endcode
	double dy;
	/// The X scale of the texture
	float uvScale;
	/// The X offset of the texture
	float uvOffset;
	/// height of the wall for rendering. Does not affect collision
	float height;
	/// The wall's box2d body ID
	b2BodyId bodyId;
};

// Utility functions are in Structs/level.h
struct Level
{
	/// The level's display name
	char name[32];
	/// The level's display course number, with -1 being none
	short courseNum;

	/// The list of actors in the level
	List actors;
	/// The list of walls in the level
	List walls;
	/// The list of triggers in the level
	List triggers;
	/// The list of models in the level
	List models;

	/// Indicates if the level has a ceiling. If false, the level will use a sky instead
	bool hasCeiling;
	/// The fully qualified texture name (texture/level_uvtest.gtex instead of level_uvtest)
	char ceilOrSkyTex[48];
	/// The fully qualified texture name (texture/level_uvtest.gtex instead of level_uvtest)
	char floorTex[48];

	/// The music name, or "none" for no music
	char music[32];

	/// The color of the fog
	uint fogColor;
	/// The distance from the player at which the fog begins to fade in
	double fogStart;
	/// The distance from the player at which the fog is fully opaque
	double fogEnd;

	/// The ID of the box2d world
	b2WorldId worldId;

	/// The player object
	Player player;
};

// Utility functions are in Structs/ray.h
struct RayCastResult
{
	/// The point of collision
	Vector2 collisionPoint;
	/// The distance to the collision
	double distance;
	/// Whether the ray collided with anything
	bool collided;
	/// The wall that was collided with
	Wall *collisionWall;
};

struct TextBox
{
	/// The text to display
	char *text;
	/// The number of rows per page
	int rows;
	/// The number of columns to display
	int cols;
	/// The X position of the text box
	int x;
	/// The Y position of the text box
	int y;

	/// The horizontal alignment of the text box
	int hAlign;
	/// The vertical alignment of the text box
	int vAlign;
	/// The text box theme
	int theme;

	/// The function to call when the text box is closed
	TextBoxCloseFunction Close;
};

struct Options
{
	/// Checksum of the options struct (helps prevent corruption)
	ushort checksum;

	/* Controls */

	/// Whether the game is in controller mode
	bool controllerMode;
	/// The look speed (it affects controller speed too)
	double mouseSpeed;
	/// The strength of the rumble
	float rumbleStrength;
	/// Whether to invert the camera X axis (controller only)
	bool cameraInvertX;
	/// Whether to swap the controller A and B buttons
	bool controllerSwapOkCancel;

	/* Video */

	/// The renderer to use
	Renderer renderer;
	/// Whether the game is fullscreen
	bool fullscreen;
	/// Whether vsync is enabled
	bool vsync;
	/// The MSAA level
	OptionsMsaa msaa;
	/// Whether to use mipmaps
	bool mipmaps;
	/// Whether to prefer Wayland over X11
	bool preferWayland;
	/// Whether to drop to 30 fps when the window is not focused
	bool limitFpsWhenUnfocused;

	/* Audio */

	/// The volume of the music
	double musicVolume;
	/// The volume of the sound effects
	double sfxVolume;
	/// The master volume
	double masterVolume;
} __attribute__((packed)); // This is packed because it is saved to disk

struct ModelHeader
{
	/// The "magic" for the header, should be "MSH"
	char sig[4];
	/// The number of indices in the model
	uint indexCount;
	/// The number of vertices in the model
	uint vertexCount;
	/// The "magic" for the data, should be "DAT"
	char dataSig[4];
} __attribute__((packed));

struct Model
{
	ModelHeader header;
	size_t id;
	char *name;

	/// The number of vertices in the model
	uint vertexCount;
	/// The number of indices in the model
	uint indexCount;
	/// Packed vertex data, (X Y Z) (U V) (NX NY NZ)
	float *vertexData;
	/// Index data
	uint *indexData;
};

// Global state of the game
struct GlobalState
{
	/// Current level
	Level *level;

	/// State update function
	FrameUpdateFunction UpdateGame;
	/// State render function
	FrameRenderFunction RenderGame;
	/// The current state of the game
	CurrentState currentState;
	/// The number of physics frames that have passed since the last game state change
	ulong physicsFrame;

	/// Player health
	int hp;
	/// Player max health
	int maxHp;
	/// Player ammo
	int ammo;
	/// Player max ammo
	int maxAmmo;
	/// The number of coins the player has
	int coins;
	/// The number of blue coins the player has
	int blueCoins;

	/// Whether the text box is active
	bool textBoxActive;
	/// The text box
	TextBox textBox;
	/// The current page of the text box
	int textBoxPage;

	/// The camera
	Camera *cam;
	/// The Y position of the camera
	double cameraY;
	/// The scale of the UI.
	double uiScale;

	/// Game options
	Options options;
	// Whether the audio system has been started successfully
	bool isAudioStarted;
	/// background music
	Mix_Music *music;
	/// sound effects
	Mix_Chunk *channels[SFX_CHANNEL_COUNT];

	// The path to the executable
	char executablePath[261];
	/// The path to the executable folder
	char executableFolder[261];
	/// Whether to freeze the event loop. This should only be used for debugging.
	bool freezeEvents;
	/// Request to exit the game
	bool requestExit;
};

// Actor (interactable/moving wall) struct
struct Actor
{
	/// The position of the actor
	Vector2 position;
	/// The rotation of the actor
	float rotation;
	/// y position for rendering. Does not affect collision
	float yPosition;
	/// should the actor cast a shadow?
	bool showShadow;
	/// size of the shadow
	float shadowSize;
	// Optional model for the actor, if not NULL, will be rendered instead of the wall
	Model *actorModel;
	/// Texture for the model
	char *actorModelTexture;

	/// The actor's wall, in global space
	Wall *actorWall;

	/// type of actor. do not change this after creation.
	int actorType;
	/// The function to call when the actor is initialized. This should only be called once, when the actor is created.
	ActorInitFunction Init;
	/// The function to call when the actor is updated. This should be called every tick.
	ActorUpdateFunction Update;
	/// The function to call when the actor is destroyed. This should only be called once, when the actor is destroyed.
	ActorDestroyFunction Destroy;
	/// The function to call when the actor receives a signal.
	ActorSignalHandlerFunction SignalHandler;
	/// List of signals the actor is listening for
	List listeningFor;

	// extra parameters for the actor. saved in level data, so can be used during Init
	byte paramA;
	byte paramB;
	byte paramC;
	byte paramD;

	// health. may be unused for some actors
	int health;
	/// extra data for the actor
	void *extra_data;

	/// The actor's box2d body ID
	b2BodyId bodyId;
};

struct Asset
{
	/// The compressed size of the asset, excluding the header
	uint compressedSize;
	/// The decompressed size of the asset
	uint size;
	/// The type of the asset
	AssetType type;
	/// The data of the asset
	byte *data;
};

struct Image
{
	/// The size of the pixel data (width * height * 4)
	uint pixelDataSize;
	/// The width of the image
	uint width;
	/// The height of the image
	uint height;
	/// The ID of the image. This is generated at runtime and not consistent between runs.
	uint id;
	/// The name of the image
	char *name;
	/// The pixel data of the image
	byte *pixelData;
};

struct Trigger
{
	/// The center position of the trigger
	Vector2 position;
	/// The rotation of the trigger
	double rotation;
	/// The size of the trigger
	Vector2 extents;
	/// The command to execute when this trigger is triggered
	char command[64];
	/// The flags set on this trigger
	uint flags;
};

struct Font
{
	/// The texture width of one character
	uint width;
	/// The texture height (including below baseline)
	uint texture_height;
	/// The pixel coordinate of the baseline
	uint baseline;
	/// The pixels between characters
	uint char_spacing;
	/// The pixels between lines
	uint line_spacing;
	/// The width of a space character
	uint space_width;
	/// The default size of the font, used for calculating scale
	uint default_size;
	/// The number of characters in the font
	uint char_count;

	/// The texture this font uses (fully qualified)
	char texture[48];
	/// The characters in the font
	char chars[128];
	/// The width of each character, index directly by the character
	byte char_widths[128];

	/// The image loaded from the texture
	Image *image;
};

#pragma endregion

#endif //GAME_DEFINES_H
