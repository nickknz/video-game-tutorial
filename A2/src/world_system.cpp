// Header
#include "world_system.hpp"
#include "world_init.hpp"

// stlib
#include <cassert>
#include <sstream>

#include "physics_system.hpp"

// Game configuration
const size_t MAX_TURTLES = 15;
const size_t MAX_FISH = 5;
const size_t TURTLE_DELAY_MS = 2000 * 3;
const size_t FISH_DELAY_MS = 5000 * 3;
const size_t PEBBLE_DELAY_MS = 200 * 3;

// Create the fish world
WorldSystem::WorldSystem()
	: points(0)
	, next_turtle_spawn(0.f)
	, next_fish_spawn(0.f) 
	, next_pebble_spawn(0.f) {
	// Seeding rng with random device
	rng = std::default_random_engine(std::random_device()());
}

WorldSystem::~WorldSystem() {
	// Destroy music components
	if (background_music != nullptr)
		Mix_FreeMusic(background_music);
	if (salmon_dead_sound != nullptr)
		Mix_FreeChunk(salmon_dead_sound);
	if (salmon_eat_sound != nullptr)
		Mix_FreeChunk(salmon_eat_sound);
	Mix_CloseAudio();

	// Destroy all created components
	registry.clear_all_components();

	// Close the window
	glfwDestroyWindow(window);
}

// Debugging
namespace {
	void glfw_err_cb(int error, const char *desc) {
		fprintf(stderr, "%d: %s", error, desc);
	}
}

// World initialization
// Note, this has a lot of OpenGL specific things, could be moved to the renderer
GLFWwindow* WorldSystem::create_window() {
	///////////////////////////////////////
	// Initialize GLFW
	glfwSetErrorCallback(glfw_err_cb);
	if (!glfwInit()) {
		fprintf(stderr, "Failed to initialize GLFW");
		return nullptr;
	}

	//-------------------------------------------------------------------------
	// If you are on Linux or Windows, you can change these 2 numbers to 4 and 3 and
	// enable the glDebugMessageCallback to have OpenGL catch your mistakes for you.
	// GLFW / OGL Initialization
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
#if __APPLE__
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
	glfwWindowHint(GLFW_RESIZABLE, 0);

	// Create the main window (for rendering, keyboard, and mouse input)
	window = glfwCreateWindow(window_width_px, window_height_px, "Salmon Game Assignment", nullptr, nullptr);
	if (window == nullptr) {
		fprintf(stderr, "Failed to glfwCreateWindow");
		return nullptr;
	}

	// Setting callbacks to member functions (that's why the redirect is needed)
	// Input is handled using GLFW, for more info see
	// http://www.glfw.org/docs/latest/input_guide.html
	glfwSetWindowUserPointer(window, this);
	auto key_redirect = [](GLFWwindow* wnd, int _0, int _1, int _2, int _3) { ((WorldSystem*)glfwGetWindowUserPointer(wnd))->on_key(_0, _1, _2, _3); };
	auto cursor_pos_redirect = [](GLFWwindow* wnd, double _0, double _1) { ((WorldSystem*)glfwGetWindowUserPointer(wnd))->on_mouse_move({ _0, _1 }); };
	glfwSetKeyCallback(window, key_redirect);
	glfwSetCursorPosCallback(window, cursor_pos_redirect);

	//////////////////////////////////////
	// Loading music and sounds with SDL
	if (SDL_Init(SDL_INIT_AUDIO) < 0) {
		fprintf(stderr, "Failed to initialize SDL Audio");
		return nullptr;
	}
	if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) == -1) {
		fprintf(stderr, "Failed to open audio device");
		return nullptr;
	}

	background_music = Mix_LoadMUS(audio_path("music.wav").c_str());
	salmon_dead_sound = Mix_LoadWAV(audio_path("salmon_dead.wav").c_str());
	salmon_eat_sound = Mix_LoadWAV(audio_path("salmon_eat.wav").c_str());

	if (background_music == nullptr || salmon_dead_sound == nullptr || salmon_eat_sound == nullptr) {
		fprintf(stderr, "Failed to load sounds\n %s\n %s\n %s\n make sure the data directory is present",
			audio_path("music.wav").c_str(),
			audio_path("salmon_dead.wav").c_str(),
			audio_path("salmon_eat.wav").c_str());
		return nullptr;
	}

	return window;
}

void WorldSystem::init(RenderSystem* renderer_arg) {
	this->renderer = renderer_arg;
	// Playing background music indefinitely
	Mix_PlayMusic(background_music, -1);
	fprintf(stderr, "Loaded music\n");

	// Set all states to default
    restart_game();
}

// Update our game world
bool WorldSystem::step(float elapsed_ms_since_last_update) {
	// Updating window title with points
	std::stringstream title_ss;
	title_ss << "Points: " << points;
	glfwSetWindowTitle(window, title_ss.str().c_str());

	// Remove debug info from the last step
	while (registry.debugComponents.entities.size() > 0)
	    registry.remove_all_components_of(registry.debugComponents.entities.back());

	// Removing out of screen entities
	auto& motion_container = registry.motions;

	// Remove entities that leave the screen on the left side
	// Iterate backwards to be able to remove without unterfering with the next object to visit
	// (the containers exchange the last element with the current)
	for (int i = (int)motion_container.components.size()-1; i>=0; --i) {
	    Motion& motion = motion_container.components[i];
		if ((motion.position.x + abs(motion.scale.x) < 0.f) || (motion.position.x - abs(motion.scale.x) > 1200.f)) {
			if(!registry.players.has(motion_container.entities[i]) ) // don't remove the player
				registry.remove_all_components_of(motion_container.entities[i]);
		}
	}

	// Spawning new turtles
	next_turtle_spawn -= elapsed_ms_since_last_update * current_speed;
	if (registry.hardShells.components.size() <= MAX_TURTLES && next_turtle_spawn < 0.f) {
		// Reset timer
		next_turtle_spawn = (TURTLE_DELAY_MS / 2) + uniform_dist(rng) * (TURTLE_DELAY_MS / 2);
		// Create turtle
		Entity entity = createTurtle(renderer, {0,0});
		// Setting random initial position and constant velocity
		Motion& motion = registry.motions.get(entity);
		motion.position =
			vec2(window_width_px -200.f,
				 50.f + uniform_dist(rng) * (window_height_px - 100.f));
		motion.velocity = vec2(-100.f, 0.f);
	}

	// Spawning new fish
	next_fish_spawn -= elapsed_ms_since_last_update * current_speed;
	if (registry.softShells.components.size() <= MAX_FISH && next_fish_spawn < 0.f) {
		// !!!  TODO A1: Create new fish with createFish({0,0}), as for the Turtles above
		// Reset timer
		next_fish_spawn = (FISH_DELAY_MS / 2) + uniform_dist(rng) * (FISH_DELAY_MS / 2);
		// Create fish
		Entity entity = createFish(renderer, {0,0});
		// Setting random initial position and constant velocity
		Motion& motion = registry.motions.get(entity);
		motion.position =
			vec2(window_width_px -200.f,
				 50.f + uniform_dist(rng) * (window_height_px - 100.f));
		motion.velocity = vec2(-200.f, 0.f);
	}

	// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	// TODO A2: HANDLE PEBBLE SPAWN HERE
	// DON'T WORRY ABOUT THIS UNTIL ASSIGNMENT 2
	// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

	// Spawning new pebbles
	next_pebble_spawn -= elapsed_ms_since_last_update * current_speed;
	if (next_pebble_spawn < 0.f) {
		// Reset timer
		next_pebble_spawn = (PEBBLE_DELAY_MS / 2) + uniform_dist(rng) * (PEBBLE_DELAY_MS / 2);

		// create pebble
		Motion& player_salmon_motion = registry.motions.get(player_salmon);
		float radius = 30 * (uniform_dist(rng) + 0.3f); // range 0.3 .. 1.3
		Entity pebble = createPebble(player_salmon_motion.position, { radius, radius });
		float brightness = uniform_dist(rng) * 0.5 + 0.5;
		registry.colors.insert(pebble, { brightness, brightness, brightness});

		// set motion 
		Motion& motion = registry.motions.get(pebble);
		float rand_angle = player_salmon_motion.angle + (uniform_dist(rng) * 0.6 - 0.3f);
		// float rand_angle = player_salmon_motion.angle;

		float rand_vel_x = 200.f + (uniform_dist(rng) - 0.5f) * 100;

		// decompose the x velocity
		float cos_x_vel = cos(rand_angle) * rand_vel_x;
		float sin_x_vel = sin(rand_angle) * rand_vel_x;
		motion.velocity.x = cos_x_vel;
		motion.velocity.y = sin_x_vel;
	}

	// Processing the salmon state
	assert(registry.screenStates.components.size() <= 1);
    ScreenState &screen = registry.screenStates.components[0];

    float min_timer_ms = 3000.f;
	for (Entity entity : registry.deathTimers.entities) {
		// progress timer
		DeathTimer& timer = registry.deathTimers.get(entity);
		timer.timer_ms -= elapsed_ms_since_last_update;
		if(timer.timer_ms < min_timer_ms){
			min_timer_ms = timer.timer_ms;
		}

		// restart the game once the death timer expired
		if (timer.timer_ms < 0) {
			registry.deathTimers.remove(entity);
			screen.screen_darken_factor = 0;
            restart_game();
			return true;
		}
	}
	// reduce window brightness if any of the present salmons is dying
	screen.screen_darken_factor = 1 - min_timer_ms / 3000;

	// !!! TODO A1: update LightUp timers and remove if time drops below zero, similar to the death timer
	for (Entity entity : registry.lightUpTimers.entities) {
		// progress timer
		LightUp& timer = registry.lightUpTimers.get(entity);
		timer.timer_ms -= elapsed_ms_since_last_update;

		// delete timer once the lightup timer expired
		if (timer.timer_ms < 0) {
			registry.lightUpTimers.remove(entity);
		}
	}

	return true;
}

// Reset the world state to its initial state
void WorldSystem::restart_game() {
	// Debugging for memory/component leaks
	registry.list_all_components();
	printf("Restarting\n");

	// Reset the game speed
	current_speed = 1.f;

	// Remove all entities that we created
	// All that have a motion, we could also iterate over all fish, turtles, ... but that would be more cumbersome
	while (registry.motions.entities.size() > 0)
	    registry.remove_all_components_of(registry.motions.entities.back());

	// Debugging for memory/component leaks
	registry.list_all_components();

	// Create a new salmon
	player_salmon = createSalmon(renderer, { 100, 200 });
	registry.colors.insert(player_salmon, {1, 0.8f, 0.8f});

	// !! TODO A2: Enable static pebbles on the ground, for reference
	// Create pebbles on the floor, use this for reference
	
	// for (uint i = 0; i < 20; i++) {
	// 	int w, h;
	// 	glfwGetWindowSize(window, &w, &h);
	// 	float radius = 30 * (uniform_dist(rng) + 0.3f); // range 0.3 .. 1.3
	// 	Entity pebble = createPebble({ uniform_dist(rng) * w, h - uniform_dist(rng) * 20 }, 
	// 		         { radius, radius });
	// 	float brightness = uniform_dist(rng) * 0.5 + 0.5;
	// 	registry.colors.insert(pebble, { brightness, brightness, brightness});
	// }
	
	// Entity line = createLine({250, 150}, {100, 100});
	// registry.colors.insert(line, {1, 0.8f, 0.8f});
}

// Compute collisions between entities
void WorldSystem::handle_collisions() {
	// Loop over all collisions detected by the physics system
	auto& collisionsRegistry = registry.collisions;
	for (uint i = 0; i < collisionsRegistry.components.size(); i++) {
		// The entity and its collider
		Entity entity = collisionsRegistry.entities[i];
		Entity entity_other = collisionsRegistry.components[i].other_entity;

		// For now, we are only interested in collisions that involve the salmon
		if (registry.players.has(entity)) {
			//Player& player = registry.players.get(entity);

			// Checking Player - HardShell collisions
			if (registry.hardShells.has(entity_other)) {
				// initiate death unless already dying
				if (!registry.deathTimers.has(entity)) {
					// Scream, reset timer, and make the salmon sink
					registry.deathTimers.emplace(entity);
					Mix_PlayChannel(-1, salmon_dead_sound, 0);

					// !!! TODO A1: change the salmon orientation and color on death
					Motion& motion = registry.motions.get(entity);
					motion.angle = -PI;
					motion.velocity = (vec2) {0.f, -100.f};

					// change color to red
					vec3& salmon_color = registry.colors.get(entity);
					salmon_color = {1, 0.f, 0.f};
				}
			}
			// Checking Player - SoftShell collisions
			else if (registry.softShells.has(entity_other)) {
				if (!registry.deathTimers.has(entity)) {
					// chew, count points, and set the LightUp timer
					registry.remove_all_components_of(entity_other);
					Mix_PlayChannel(-1, salmon_eat_sound, 0);
					++points;

					// !!! TODO A1: create a new struct called LightUp in components.hpp and 
					// add an instance to the salmon entity by modifying the ECS registry
					registry.lightUpTimers.emplace(entity);
				}
			}
		}
	}

	// Remove all collisions from this simulation step
	registry.collisions.clear();
}

// Should the game be over ?
bool WorldSystem::is_over() const {
	return bool(glfwWindowShouldClose(window));
}

// On key callback
void WorldSystem::on_key(int key, int, int action, int mod) {
	// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	// TODO A1: HANDLE SALMON MOVEMENT HERE
	// key is of 'type' GLFW_KEY_
	// action can be GLFW_PRESS GLFW_RELEASE GLFW_REPEAT
	// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

	if (!registry.deathTimers.has(player_salmon)) {
		Motion& motion = registry.motions.get(player_salmon);
		
		if (action == GLFW_PRESS) {
			if (key == GLFW_KEY_LEFT) {
				motion.velocity.x -= 200.f;
			} else if (key == GLFW_KEY_UP) {
				motion.velocity.y -= 100.f;
			} else if (key == GLFW_KEY_RIGHT) {
				motion.velocity.x += 200.f;
			} else if (key == GLFW_KEY_DOWN) {
				motion.velocity.y += 100.f;
			}
		} else if (action == GLFW_RELEASE) {
			if (key == GLFW_KEY_LEFT) {
				motion.velocity.x += 200.f;
			} else if (key == GLFW_KEY_UP) {
				motion.velocity.y += 100.f;
			} else if (key == GLFW_KEY_RIGHT) {
				motion.velocity.x -= 200.f;
			} else if (key == GLFW_KEY_DOWN) {
				motion.velocity.y -= 100.f;
			}
		} else if (action == GLFW_REPEAT) {
			// currently I do not need it. Might be used in the future
		}
		// printf("pos: %f\n", motion.position.x);
	}
	
	// Resetting game
	if (action == GLFW_RELEASE && key == GLFW_KEY_R) {
		int w, h;
		glfwGetWindowSize(window, &w, &h);

        restart_game();
	}

	// Debugging
	if (key == GLFW_KEY_D) {
		if (action == GLFW_RELEASE)
			debugging.in_debug_mode = false;
		else
			debugging.in_debug_mode = true;
	}

	// Control the current speed with `<` `>`
	if (action == GLFW_RELEASE && (mod & GLFW_MOD_SHIFT) && key == GLFW_KEY_COMMA) {
		current_speed -= 0.1f;
		printf("Current speed = %f\n", current_speed);
	}
	if (action == GLFW_RELEASE && (mod & GLFW_MOD_SHIFT) && key == GLFW_KEY_PERIOD) {
		current_speed += 0.1f;
		printf("Current speed = %f\n", current_speed);
	}
	current_speed = fmax(0.f, current_speed);
}

void WorldSystem::on_mouse_move(vec2 mouse_position) {
	// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	// TODO A1: HANDLE SALMON ROTATION HERE
	// xpos and ypos are relative to the top-left of the window, the salmon's
	// default facing direction is (1, 0)
	// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	
	if (!registry.deathTimers.has(player_salmon)) {
		Motion& salmon_motion = registry.motions.get(player_salmon);

		float x_value = mouse_position.x - salmon_motion.position.x;
		float y_value = mouse_position.y - salmon_motion.position.y;
		salmon_motion.angle = atan2(y_value, x_value);
	}
	(vec2)mouse_position; // dummy to avoid compiler warning
}
