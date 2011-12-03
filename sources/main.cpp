#include "SDL/SDL.h"
#include "GL/gl.h"
#include <stdio.h>
#include <stdlib.h>
#include "audio.hpp"
#include "objects.hpp"
#include "math.hpp"
#include "level.hpp"
#include "player.hpp"
#include "sfx.hpp"

int xRes = 640;
int yRes = 480;

int hiScore = 0;
int score = 0;

const float mouseCursor[] = {
	-6, -6,		-2, -2,
	6, -6,		2, -2,
	-6, 6,		-2, 2,
	6, 6,		2, 2	
};

static const struct { char c; const float* pObject; } fontMapping[] = {
	{ '0', obj_0 },
	{ '1', obj_1 },
	{ '2', obj_2 },
	{ '3', obj_3 },
	{ '4', obj_4 },
	{ '5', obj_5 },
	{ '6', obj_6 },
	{ '7', obj_7 },
	{ '8', obj_8 },
	{ '9', obj_9 },
	{ 's', obj_5 },
	{ 'c', obj_c },
	{ 'o', obj_0 },
	{ 'r', obj_r },
	{ 'e', obj_e },
	{ 'h', obj_h },
	{ 'i', obj_i },
	{ '/', obj_slash },
	{ '$', obj_orb },
	{ ':', obj_colon },
	{ 'g', obj_6 },
	{ 'a', obj_a },
	{ 'm', obj_m },
	{ 'v', obj_v },
	{ 'p', obj_p },
	{ 't', obj_t },
};

enum GameState { State_LevelFadeIn, State_Level, State_LevelFadeOut, State_GameOver, State_Title, State_StartGame, State_ToTitle };

void render_object(const float* pObject)
{
	glVertexPointer(2, GL_FLOAT, 0, pObject + 1);
	glDrawArrays(GL_LINES, 0, (int)*pObject);
}

void print(const Vector2& position, const char* pFormat, ...)
{
	va_list ap;
	va_start(ap, pFormat);
	char buffer[100];
	vsnprintf(buffer, sizeof(buffer), pFormat, ap);
	va_end(ap);
	
	glPushMatrix();
	glTranslatef(position.x, position.y, 0);
	glScalef(20, 20, 0);
	const char* pText = buffer;
	while(*pText)
	{
		for(int i = 0; i < (int)(sizeof(fontMapping) / sizeof(fontMapping[0])); i++)
		{
			if(fontMapping[i].c == *pText)
			{
				render_object(fontMapping[i].pObject);
				break;
			}
		}
		glTranslatef(1, 0, 0);
		pText++;
	}
	glPopMatrix();
}

int main(int argc, char *argv[])
{
	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_TIMER | SDL_INIT_AUDIO) < 0)
	{
		fprintf(stderr, "Unable to init SDL: %s\n", SDL_GetError());
		exit(1);
	}
	atexit(SDL_Quit);
	
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_SWAP_CONTROL, 1);
	
	bool fullscreen = true;
	int forceWidth = 0;
	int forceHeight = 0;
	for(int i = 1; i < argc; i++)
	{
		if(strcmp(argv[i], "--width") == 0 && i+1 < argc)
		{
			forceWidth = atoi(argv[i+1]);
			i++;
		}
		else if(strcmp(argv[i], "--height") == 0 && i+1 < argc)
		{
			forceHeight = atoi(argv[i+1]);
			i++;
		}
		else if(strcmp(argv[i], "--windowed") == 0)
		{
			fullscreen = false;
		}
		else
		{
			fprintf(stderr, "Unknown option '%s'\n", argv[i]);
		}
	}
	
	if(fullscreen)
	{
		SDL_Rect** pModes = SDL_ListModes(NULL, SDL_OPENGL | SDL_FULLSCREEN);
		if(pModes == NULL)
		{
			fprintf(stderr, "No modes available\n");
			exit(1);
		}
	
		if(pModes != (SDL_Rect**)-1)
		{
			for(int i = 0; pModes[i]; i++)
			{
				if(pModes[i]->w <= 1024 && pModes[i]->h <= 768)
				{
					xRes = pModes[0]->w;
					yRes = pModes[0]->h;
					break;
				}
			}
		}
	}
	
	if(forceWidth)
	{
		xRes = forceWidth;
	}
	if(forceHeight)
	{
		yRes = forceHeight;
	}
	
	SDL_Surface* pScreen = SDL_SetVideoMode(xRes, yRes, 32, SDL_OPENGL | (fullscreen ? SDL_FULLSCREEN : 0));
	if(pScreen == NULL)
	{
		fprintf(stderr, "Unable to set video mode: %s\n", SDL_GetError());
		exit(1);
	}
	
	SDL_Joystick* pPad = NULL;
	if(SDL_NumJoysticks() > 0)
	{
		pPad = SDL_JoystickOpen(0);
		printf("Joystick initialized: %s\n", SDL_JoystickName(0));
		SDL_JoystickEventState(SDL_ENABLE);
	}
	
	SDL_ShowCursor(0);
	
	Audio* pAudio = new Audio;
	
	int currentLevel = 0;
	Level level;
	Player player;
	
	glEnable(GL_LINE_SMOOTH);
	glLineWidth(2);
	
	glEnableClientState(GL_VERTEX_ARRAY);
	
	glClearColor(0, 1, 0, 0);
	bool quit = false;
	bool quitGame = false;
	unsigned int lastTicks = SDL_GetTicks();
	Input input;
	float scale = 1.0f;
	float stateTime = 0;
	float timeLeft = 60;
	float timeIncrement = 20;
	bool pause = false;
	GameState gameState = State_ToTitle;
	GameState nextState = State_Title;
	Vector2 mousePosition(xRes / 2, yRes / 2);
	while(!quit)
	{
		float timeStep;
		unsigned int ticks;
		do
		{
			ticks = SDL_GetTicks();
			timeStep = (ticks - lastTicks) / 1000.0f;
			if(timeStep <= 1.0 / 120)
			{
				SDL_Delay(1);
			}
		} while(timeStep <= 1.0 / 120);
		lastTicks = ticks;
		
		if(timeStep > 1.0 / 10)
		{
			timeStep = 1.0 / 10;
		}
		
		SDL_Event event;
		
		input.buttonTriggered = false;
		input.playDead = false;

		while(SDL_PollEvent(&event))
		{
			switch(event.type)
			{
			case SDL_KEYDOWN:
				switch(event.key.keysym.sym)
				{
				case SDLK_ESCAPE:
					if(gameState == State_LevelFadeIn || gameState == State_Level || gameState == State_LevelFadeOut)
					{
						quitGame = true;
					}
					else
					{
						quit = true;
					}
					break;
				case SDLK_LSHIFT:
				case SDLK_SPACE:
					input.button = input.buttonTriggered = true;
					break;
				case SDLK_p:
					if(gameState == State_Level)
					{
						pause = !pause;
					}
					break;
				default:
					break;
				}
				break;
			case SDL_KEYUP:
				input.button = false;
				break;
			case SDL_JOYBUTTONDOWN:
				input.button = input.buttonTriggered = true;
				break;
			case SDL_JOYBUTTONUP:
				input.button = false;
				break;
			case SDL_MOUSEMOTION:
				mousePosition.x = event.motion.x;
				mousePosition.y = event.motion.y;
				break;
			case SDL_MOUSEBUTTONDOWN:
				input.button = input.buttonTriggered = true;
				break;
			case SDL_MOUSEBUTTONUP:
				input.button = false;
				break;
			case SDL_QUIT:
				quit = true;
				break;
			}
		}
		
		if(pPad)
		{
			input.stick = Vector2(SDL_JoystickGetAxis(pPad, 0) / 32768.0, SDL_JoystickGetAxis(pPad, 1) / 32768.0);
		}
		else
		{
			input.stick = (mousePosition - Vector2(xRes/2, yRes/2)) * (2.0 / yRes);
		}
		
		switch(gameState)
		{
		case State_LevelFadeIn:
			player.update(0, input);
			if(stateTime < 1)
			{
				scale = pow(2, (1-cosf((1 - stateTime) * 1.5f)) * 8);
			}
			else
			{
				scale = 1;
				gameState = nextState;
			}
			break;
		case State_Level:
			if(!pause)
			{
				player.update(timeStep, input);
			}
			if(level.numOrbsLeft() == 0)
			{
				score += (int)timeLeft * 10;
				gameState = State_LevelFadeOut;
				nextState = State_LevelFadeIn;
				stateTime = 0;
				pause = false;
			}
			else
			{
				if(!pause)
				{
					timeLeft -= timeStep;
				}
				if(timeLeft < 0 || quitGame)
				{
					timeLeft = 0;
					gameState = State_GameOver;
					stateTime = 0;
					FxSynth::playSfx(sfx_gameover, 1, true);
					pause = false;
				}
			}
			break;
		case State_LevelFadeOut:
			input.playDead = true;
			player.update(timeStep, input);
			scale = pow(2, ((1-cosf(stateTime)) * 1.5f) * 8);
			if(stateTime > 1)
			{
				currentLevel++;
				if(currentLevel >= numLevels)
				{
					currentLevel = 0;
				}
				level.initialize(pLevels[currentLevel]);
				player.initialize(&level);
				gameState = nextState;
				if(nextState == State_LevelFadeIn)
				{
					FxSynth::playSfx(sfx_level_fade_in);
					nextState = State_Level;
					timeLeft += timeIncrement;
					timeIncrement *= 0.85f;
				}
				stateTime = 0;
			}
			break;
		case State_Title:
			input.playDead = true;
			player.update(timeStep, input);
			if(input.buttonTriggered)
			{
				FxSynth::playSfx(sfx_collect, 1, true);
				gameState = State_LevelFadeOut;
				nextState = State_StartGame;
				stateTime = 0;
			}
			break;
		case State_StartGame:
			FxSynth::playSfx(sfx_level_fade_in);
			gameState = State_LevelFadeIn;
			nextState = State_Level;
			timeLeft = 60;
			timeIncrement = 20;
			currentLevel = 0;
			level.initialize(pLevels[currentLevel]);
			player.initialize(&level);
			stateTime = 0;
			score = 0;
			quitGame = false;
			break;
		case State_GameOver:
			input.playDead = true;
			player.update(timeStep, input);
			if(stateTime > 4)
			{
				gameState = State_LevelFadeOut;
				nextState = State_ToTitle;
				stateTime = 0;
			}
			break;
		case State_ToTitle:
			gameState = State_LevelFadeIn;
			nextState = State_Title;
			stateTime = 0;
			level.initialize(&level99);
			player.initialize(&level);
			FxSynth::playSfx(sfx_level_fade_in);
			break;
		}
		
		if(score > hiScore)
		{
			hiScore = score;
		}
		
		glClearColor(0, 0.1f, 0, 0);
		
		glClear(GL_COLOR_BUFFER_BIT);
		glDepthFunc(GL_ALWAYS);
		
		glViewport(0, 0, xRes, yRes);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0, xRes, yRes, 0, -1000, 1000);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		
		glColor4f(0.8, 1, 0.8, 0.3);
		
		if(!pPad)
		{
			glPushMatrix();
			glTranslatef(mousePosition.x, mousePosition.y, 0);
			glVertexPointer(2, GL_FLOAT, 0, mouseCursor);
			glDrawArrays(GL_LINES, 0, sizeof(mouseCursor) / (sizeof(float) * 2));
			glPopMatrix();
		}
		
		print(Vector2(10, 10), "hi %d", hiScore);
		print(Vector2(xRes - 20 * 11 - 10, 10), "score %d", score);
		if(gameState != State_Title && nextState != State_Title && nextState != State_StartGame)
		{
			print(Vector2(10, yRes - 32), "$%d", level.numOrbsLeft());
			int intTimeLeft = ceil(timeLeft);
			print(Vector2(xRes / 2 - 40, yRes - 32), "%d:%02d", intTimeLeft / 60, intTimeLeft % 60);
		}
		
		if(gameState == State_Title && fmodf(stateTime, 1) < 0.5f)
		{
			print(Vector2(xRes/2 - 110, yRes/2 + 50), "press start");
		}
		else if(gameState == State_GameOver && fmodf(stateTime, 1) < 0.5f)
		{
			print(Vector2(xRes/2 - 90, yRes/2 + 50), "game over");
		}
		
		glScalef(scale, scale, 1);
		glTranslatef(xRes/2/scale - player.getPosition().x * 100, yRes/2/scale - player.getPosition().y * 100, 0);
		glScalef(100, 100, 1);

		level.render();
		
		player.render(gameState == State_Level);
		
		SDL_GL_SwapBuffers();
		
		stateTime += timeStep;
	}
	
	delete pAudio;
	
	if(pPad)
	{
		SDL_JoystickClose(pPad);
	}
	
	return 0;
}
