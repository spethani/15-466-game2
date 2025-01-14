#include "PlayMode.hpp"

#include "LitColorTextureProgram.hpp"

#include "DrawLines.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <random>
#include <vector>

GLuint hexapod_meshes_for_lit_color_texture_program = 0;
Load< MeshBuffer > hexapod_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("game2.pnct"));
	hexapod_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});

Load< Scene > hexapod_scene(LoadTagDefault, []() -> Scene const * {
	return new Scene(data_path("game2.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
		Mesh const &mesh = hexapod_meshes->lookup(mesh_name);

		scene.drawables.emplace_back(transform);
		Scene::Drawable &drawable = scene.drawables.back();

		drawable.pipeline = lit_color_texture_program_pipeline;

		drawable.pipeline.vao = hexapod_meshes_for_lit_color_texture_program;
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;
	});
});

PlayMode::PlayMode() : scene(*hexapod_scene) {
	// get pointers to scene objects for convenience:
	for (auto &transform : scene.transforms) {
		if (transform.name == "Cat") cat = &transform;
		if (transform.name.substr(0, 5) == "Mouse") {
			mouses.push_back(&transform);
		}
		if (transform.name == "Plane") plane = &transform;
	}
	if (cat == nullptr) throw std::runtime_error("Cat not found.");
	if (mouses.size() != 8) throw std::runtime_error("Mouse not found.");
	if (plane == nullptr) throw std::runtime_error("Plane not found.");

	//get pointer to camera for convenience:
	if (scene.cameras.size() != 1) throw std::runtime_error("Expecting scene to have exactly one camera, but it has " + std::to_string(scene.cameras.size()));
	camera = &scene.cameras.front();

	// set times for mouse respawn
	timeSinceRespawn = 0.0f;
	timeToRespawn = 1.0f;

	// all mice are hidden at start
	for (uint8_t i = 0; i < mouses.size(); i++) {
		mouses[i]->position.z = -1.0f;
		hiddenMice.push_back(i);
	}
}

PlayMode::~PlayMode() {
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_KEYDOWN) {
		if (evt.key.keysym.sym == SDLK_ESCAPE) {
			SDL_SetRelativeMouseMode(SDL_FALSE);
			return true;
		} else if (evt.key.keysym.sym == SDLK_a) {
			left.downs += 1;
			left.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			right.downs += 1;
			right.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.downs += 1;
			up.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			down.downs += 1;
			down.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_KEYUP) {
		if (evt.key.keysym.sym == SDLK_a) {
			left.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			right.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			down.pressed = false;
			return true;
		}
	}

	return false;
}

void PlayMode::update(float elapsed) {
	if (gameOver) {
		plane->position.z = 2.0f;
		return; // perform no updates after game over
	}

	//move cat and camera:
	{
		//combine inputs into a move:
		constexpr float PlayerSpeed = 10.0f;
		glm::vec2 move = glm::vec2(0.0f);
		if (left.pressed && !right.pressed && cat->position.y >= -10) move.x = 1.0f;
		if (!left.pressed && right.pressed && cat->position.y <= 10) move.x = -1.0f;
		if (down.pressed && !up.pressed && cat->position.x <= 10) move.y = 1.0f;
		if (!down.pressed && up.pressed && cat->position.x >= -10) move.y = -1.0f;

		//make it so that moving diagonally doesn't go faster:
		if (move != glm::vec2(0.0f)) move = glm::normalize(move) * PlayerSpeed * elapsed;

		// move cat
		glm::mat4x3 frame = cat->make_local_to_parent();
		glm::vec3 frame_right = frame[0];
		//glm::vec3 up = frame[1];
		glm::vec3 frame_forward = -frame[2];
		cat->position += move.x * frame_right + move.y * frame_forward;

		// move camera
		camera->transform->position.x = cat->position.x;
		camera->transform->position.y = cat->position.y;
	}

	// check collisions
	{
		double radius = 0.5f;
		double cat_min_x = cat->position.x - radius;
		double cat_min_y = cat->position.y - radius;
		double cat_max_x = cat->position.x + radius;
		double cat_max_y = cat->position.y + radius;
		for (uint8_t i = 0; i < mouses.size(); i++) {
			auto &transform = mouses[i];
			double mouse_min_x = transform->position.x - radius;
			double mouse_min_y = transform->position.y - radius;
			double mouse_max_x = transform->position.x + radius;
			double mouse_max_y = transform->position.y + radius;
			if ((mouse_max_x >= cat_min_x && cat_max_x >= mouse_min_x) 
				&& (mouse_max_y >= cat_min_y && cat_max_y >= mouse_min_y)
				&& transform->position.z >= 0) {
				transform->position.z = -1.0f;
				score++;
				hiddenMice.push_back(i);
			}
		}
	}

	// check for respawn of mice
	{
		timeSinceRespawn += elapsed;
		if (hiddenMice.size() == 0) {
			gameOver = true;
		}
		else if (timeSinceRespawn >= timeToRespawn) {
			timeSinceRespawn -= timeToRespawn;

			// random code taken from https://stackoverflow.com/questions/7560114/random-number-c-in-some-range
			static std::random_device rd; // obtain a random number from hardware
			static std::mt19937 gen(rd()); // seed the generator
			static std::uniform_int_distribution<> distr_location(-8, 8); // define the range

			// respawn a mouse
			int index = hiddenMice[0];
			hiddenMice.pop_front();
			mouses[index]->position.z = 1.0f;
			mouses[index]->position.x = (float) distr_location(gen);
			mouses[index]->position.y = (float) distr_location(gen);

			static std::uniform_int_distribution<> distr_time(1, 5);
			timeToRespawn = distr_time(gen);
		}
	}

	//reset button press counters:
	left.downs = 0;
	right.downs = 0;
	up.downs = 0;
	down.downs = 0;
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	//update camera aspect ratio for drawable:
	camera->aspect = float(drawable_size.x) / float(drawable_size.y);

	//set up light type and position for lit_color_texture_program:
	// TODO: consider using the Light(s) in the scene to do this
	glUseProgram(lit_color_texture_program->program);
	glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
	glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f,-1.0f)));
	glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.95f)));
	glUseProgram(0);

	glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
	glClearDepth(1.0f); //1.0 is actually the default value to clear the depth buffer to, but FYI you can change it.
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS); //this is the default depth comparison function, but FYI you can change it.

	GL_ERRORS(); //print any errors produced by this setup code

	scene.draw(*camera);

	{ //use DrawLines to overlay some text:
		glDisable(GL_DEPTH_TEST);
		float aspect = float(drawable_size.x) / float(drawable_size.y);
		DrawLines lines(glm::mat4(
			1.0f / aspect, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		));

		if (!gameOver) {
			constexpr float H = 0.09f;
			lines.draw_text("WASD moves cat; score: " + std::to_string(score),
				glm::vec3(-aspect + 0.1f * H, -1.0 + 0.1f * H, 0.0),
				glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
				glm::u8vec4(0x00, 0x00, 0x00, 0x00));
			float ofs = 2.0f / drawable_size.y;
			lines.draw_text("WASD moves cat; score: " + std::to_string(score),
				glm::vec3(-aspect + 0.1f * H + ofs, -1.0 + + 0.1f * H + ofs, 0.0),
				glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
				glm::u8vec4(0xff, 0xff, 0xff, 0x00));
		}
		else {
			constexpr float H = 0.09f;
			lines.draw_text("GAME OVER, ALL MICE SPAWNED; score: " + std::to_string(score),
				glm::vec3(-aspect + 0.1f * H, -1.0 + 0.1f * H, 0.0),
				glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
				glm::u8vec4(0x00, 0x00, 0x00, 0x00));
			float ofs = 2.0f / drawable_size.y;
			lines.draw_text("GAME OVER, ALL MICE SPAWNED; score: " + std::to_string(score),
				glm::vec3(-aspect + 0.1f * H + ofs, -1.0 + + 0.1f * H + ofs, 0.0),
				glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
				glm::u8vec4(0xff, 0xff, 0xff, 0x00));
		}
	}
}
