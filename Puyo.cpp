#include <iostream>
#include <vector>
#include <cstdio>
#include <time.h>
#include <algorithm>
#include <sstream>

#include <SDL/SDL.h>
#include <SDL/SDL_mixer.h>
#include <SDL/SDL_ttf.h>
#include <SDL/SDL_gfxPrimitives.h>

#define SCR_W 800
#define SCR_H 440
#define SCR_BPP 32

// Types /////////////////////////////////////////////////
//////////////////////////////////////////////////////////

enum Direction{ LEFT = 0, RIGHT, UP, DOWN, ROTATE};
enum PieceColor{ BLUE, GREEN, ORANGE, YELLOW, PURPLE, OJAMM };
enum PlayerType { NONE, HUM, CPU };


bool mixer_on;
bool font_on;

/* Partcles are created when we linka chain, for funsies */
struct Particle{
	Uint32 color;
	Uint32 created_at;
	Uint32 time_to_live;
	int x,y,x_vel,y_vel;
};

struct Piece{
	PieceColor color;
	bool falling;
	int x, y, w, h;
};

/* A couple are two active pieces that fall in tandum. */
struct Couple{
	Piece *p[2];
};

struct GameState {
	static const unsigned max_players = 4;
	static const unsigned player_count = 4;
	static const unsigned human_players = 1;

	PlayerType player_types[player_count];

	bool playing;
	bool paused;

	struct Board{
		static const unsigned x_offset = 40;
		static const unsigned y_offset = 40;
		static const unsigned width_in_pieces = 6;
		static const unsigned height_in_pieces = 12;
		static const unsigned piece_width = 30;
		static const unsigned piece_height = 30;
		static const unsigned height_in_px = piece_height * height_in_pieces;
		static const unsigned width_in_px = piece_width * width_in_pieces;

		bool lost, won;
		Sint32 score;
		Uint32 last_forced_move;
		Uint32 last_guided_move; // when a player is holding down
		int move_delay;

		int ojamms_pending;
		Piece *b[width_in_pieces][height_in_pieces];

	} board[player_count];

	Couple *active_couple[player_count];
	std::vector<Particle> particles;

	/* Resources */
	Mix_Chunk *chain;  // Sound played when a chain happens
	Mix_Music *bg_mus; // BG Music
	TTF_Font *font;    // Font
};

// Forward Declarations //////////////////////////////////
//////////////////////////////////////////////////////////

// Update -------------------------------
void UpdateTick(GameState*);
Couple *GenerateNewCouple(GameState*);
Direction GetRelationBetweenPieces(Piece *, Piece *);
void MoveActiveCouple(GameState*, int, Direction);
void SetAllFalling(GameState *, int);
void FallPieces(GameState*, int);
bool CheckForCombos(GameState*, int);
void BranchSearch(GameState*, int, int, int, PieceColor, std::vector<Piece*> &, std::vector<Piece*>&);
void UpdateParticles(std::vector<Particle>&);
void OjammAttack(GameState *, int);
void CPUTick(GameState*, int);

// Render --------------------------------
void RenderTick(SDL_Surface*, GameState*);
void ClearSurfaceTo(SDL_Surface *, Uint32);
void DrawBoardGrids(SDL_Surface*, GameState*);
void DrawPuyos(SDL_Surface*, GameState*);
void DrawParticles(SDL_Surface*, std::vector<Particle>&);
void DrawImpendingDoom(SDL_Surface*, GameState*);
void DrawLoserBanner(SDL_Surface*, GameState*);
void DrawWinnerBanner(SDL_Surface*, GameState*);

// GameState ------------------------------
GameState *InitNewGame();
void CleanGameState(GameState*);

// Input ----------------------------------
void HandleInput(GameState *gs, SDL_Event &event);

// Entry Point ///////////////////////////////////////////
//////////////////////////////////////////////////////////

int main(int argc, char *argv[])
{
	atexit(SDL_Quit);
	srand(time(NULL));

	if(SDL_Init(SDL_INIT_EVERYTHING) == 1){
		std::cerr << "Error initializing SDL\n";
		return -1;
	}

	SDL_WM_SetCaption("SDL Puyo Puyo", NULL);

	SDL_Surface *screen = SDL_SetVideoMode(SCR_W, SCR_H, SCR_BPP, SDL_SWSURFACE );
	if(screen == NULL){
		std::cerr << "Error in SetVideoMode\n";
		return -1;
	}

	SDL_Event event;
	Uint32 last_tick = SDL_GetTicks();

	/* Holy fucking sound initialization batman. */
	int mix_flags = MIX_INIT_MP3;
	int init_mix = Mix_Init(mix_flags);
	int audio_rate = 22050;
	Uint16 audio_format = AUDIO_S16; /* 16-bit stereo */
	int audio_channels = 2;
	int audio_buffers = 4096;

	if(Mix_OpenAudio(audio_rate, audio_format, audio_channels, audio_buffers)) {
	  std::cerr << "Unable to open audio\n";
	  mixer_on = false;
	} else {		
		if(mix_flags & init_mix != mix_flags){
			std::cerr << "Failed to init mp3 support, bg music disabled :(\n";
			std::cerr << "mixer error: " << Mix_GetError() << std::endl;
			mixer_on = false;
		} else{
			mixer_on = true;
		}
	}

	/* Init TTF */
	if(TTF_Init() == -1){
		std::cerr << "Could not initialize TTF subsystem :(\n";
		font_on = false;
	} 
	else {
		font_on = true;
	}

	GameState *gs = InitNewGame();
	if(gs == NULL){
		std::cerr << "Error initializing new game.\n";
		return -1;
	}

	gameloop:
	while(gs->playing)
	{
		while(SDL_PollEvent(&event))
		{
			if(event.type == SDL_QUIT)
				return 0;
			if(event.type == SDL_KEYDOWN)
			{
				if(event.key.keysym.sym == SDLK_ESCAPE)
					return 0;

				HandleInput(gs, event);
			}
		}

		if(SDL_GetTicks() - last_tick > 1000/60){
			UpdateTick(gs);
			last_tick = SDL_GetTicks();
		}

		RenderTick(screen, gs);
		SDL_Flip(screen);
	}

	RenderTick(screen, gs);
	SDL_Delay(5000);

	CleanGameState(gs);
	gs = InitNewGame();
	goto gameloop;
	

	if(screen)
		SDL_FreeSurface(screen);

	TTF_Quit();
	Mix_Quit();
	return 0;
}

// Update ////////////////////////////////////////////////
//////////////////////////////////////////////////////////

void UpdateTick(GameState *gs)
{
	if(!gs)
		return;


	/* Keys currently pressed down */
	Uint8 *keys = SDL_GetKeyState(NULL);
	if(SDL_GetTicks() - gs->board[0].last_guided_move > gs->board[0].move_delay)
	{
		if(keys[SDLK_s]){
			MoveActiveCouple(gs, 0, DOWN);
			gs->board[0].last_forced_move = SDL_GetTicks();
		}
		if(keys[SDLK_a])
			MoveActiveCouple(gs, 0, LEFT);
		if(keys[SDLK_d])
			MoveActiveCouple(gs, 0, RIGHT);

		gs->board[0].last_guided_move = SDL_GetTicks();
	}

	if(SDL_GetTicks() - gs->board[1].last_guided_move > gs->board[1].move_delay && gs->human_players > 1)
	{
		if(keys[SDLK_h]){
			MoveActiveCouple(gs, 1, DOWN);
			gs->board[1].last_forced_move = SDL_GetTicks();
		}
		if(keys[SDLK_g])
			MoveActiveCouple(gs, 1, LEFT);
		if(keys[SDLK_j])
			MoveActiveCouple(gs, 1, RIGHT);

		gs->board[1].last_guided_move = SDL_GetTicks();
	}

	if(SDL_GetTicks() - gs->board[2].last_guided_move > gs->board[2].move_delay && gs->human_players > 2)
	{
		if(keys[SDLK_SEMICOLON]){
			MoveActiveCouple(gs, 2, DOWN);
			gs->board[2].last_forced_move = SDL_GetTicks();
		}
		if(keys[SDLK_l])
			MoveActiveCouple(gs, 2, LEFT);
		if(keys[SDLK_QUOTE])
			MoveActiveCouple(gs, 2, RIGHT);

		gs->board[2].last_guided_move = SDL_GetTicks();
	}

	if(SDL_GetTicks() - gs->board[3].last_guided_move > gs->board[3].move_delay && gs->human_players > 3)
	{
		if(keys[SDLK_DOWN]){
			MoveActiveCouple(gs, 3, DOWN);
			gs->board[3].last_forced_move = SDL_GetTicks();
		}
		if(keys[SDLK_LEFT])
			MoveActiveCouple(gs, 3, LEFT);
		if(keys[SDLK_RIGHT])
			MoveActiveCouple(gs, 3, RIGHT);

		gs->board[3].last_guided_move = SDL_GetTicks();
	}

	int losers = 0;
	for(unsigned p = 0; p < gs->player_count; p++)
	{
		if(gs->board[p].lost == false && gs->board[p].won == false){
			if(gs->active_couple[p] == NULL){
				/* Spawn new random piece for our player. */
				 gs->active_couple[p] = GenerateNewCouple(gs);

				 Sint16 x1, x2, y1, y2;
				 x1 = gs->active_couple[p]->p[0]->x;
				 x2 = gs->active_couple[p]->p[1]->x;
				 y1 = gs->active_couple[p]->p[0]->y;
				 y2 = gs->active_couple[p]->p[1]->y;

				 if(gs->board[p].b[x1][y1] != NULL){
					 gs->board[p].lost = true;
					 delete gs->active_couple[p]->p[0];
					 delete gs->active_couple[p]->p[1];
					 delete gs->active_couple[p];
				 } else{
					 gs->board[p].b[x1][y1] = gs->active_couple[p]->p[0];
					 gs->board[p].b[x2][y2] = gs->active_couple[p]->p[1];
				 }
			} else {
				if(SDL_GetTicks() - gs->board[p].last_forced_move > 500 && gs->board[p].lost == false && gs->board[p].won == false){
					CPUTick(gs,p);
					MoveActiveCouple(gs, p, DOWN);
					gs->board[p].last_forced_move = SDL_GetTicks();
				}

				FallPieces(gs, p);
			}
		}
		else if(gs->board[p].lost){
			OjammAttack(gs,p);
			losers++;
		}
	}

	if(losers == gs->player_count - 1){
		for(unsigned p = 0; p < gs->player_count; p++){
			if(gs->board[p].lost != true){
				gs->playing = false;
			}
		}
	}

	UpdateParticles(gs->particles);
}

Couple *GenerateNewCouple(GameState *gs)
{
	if(!gs)
		return NULL;

	if(gs->max_players == 0)
		return NULL;

	Couple *c = new Couple();

	for(unsigned i = 0; i < 2; i++){
		Piece *p = new Piece();

		p->falling = true;
		p->color = (PieceColor) (rand()%5);
		p->w = gs->board[0].piece_width;
		p->h = gs->board[0].piece_height;
		p->y = 0;

		c->p[i] = p;
	}

	c->p[0]->x = 2;
	c->p[1]->x = 3;

	return c;
}

Direction GetRelationBetweenPieces(Piece *p1, Piece *p2)
{
	if(p1->x < p2->x && p1->y == p2->y)
		return RIGHT;
	else if(p1->x > p2->x && p1->y == p2->y)
		return LEFT;
	else if(p1->x == p2->x && p1->y > p2->y)
		return UP;
	else
		return DOWN;
}

void MoveActiveCouple(GameState* gs, int player, Direction dir)
{
	Couple *active_couple = gs->active_couple[player];
	if(active_couple == NULL)
		return;

	Piece *p1 = active_couple->p[0];
	Piece *p2 = active_couple->p[1];
	
	if(p1 == NULL || p2 == NULL)
		return;

	Direction relation = GetRelationBetweenPieces(p1,p2);
	int x1 = p1->x;
	int x2 = p2->x;
	int y1 = p1->y;
	int y2 = p2->y;

	if(dir == LEFT){
		if(p1->x > 0 &&
		   p2->x > 0){

			if(relation == LEFT && gs->board[player].b[x2-1][y2] != NULL ){
				return;
			}
			else if( (relation == UP || relation == DOWN) && (gs->board[player].b[x1-1][y1] != NULL ||
					                    gs->board[player].b[x2-1][y2] != NULL)){
					return;
			}
			else if( relation == RIGHT && gs->board[player].b[x1-1][y1] != NULL){
				return;
			}
			else{
				p1->x--;
				p2->x--;

				gs->board[player].b[x1][y1] = NULL;
				gs->board[player].b[x2][y2] = NULL;
				gs->board[player].b[x1-1][y1] = p1;
				gs->board[player].b[x2-1][y2] = p2;
			}
		}
	}
	else if( dir == RIGHT ) {
		if((unsigned) p1->x < gs->board[player].width_in_pieces-1  &&
		   (unsigned) p2->x < gs->board[player].width_in_pieces-1 ){
			if(relation == RIGHT && gs->board[player].b[x2+1][y2] != NULL ){
				return;
			}
			else if( (relation == UP || relation == DOWN) && (gs->board[player].b[x1+1][y1] != NULL ||
					                    gs->board[player].b[x2+1][y2] != NULL)){
					return;
			}
			else if( relation == LEFT && gs->board[player].b[x1+1][y1] != NULL){
				return;
			} else {
				p1->x++;
				p2->x++;

				gs->board[player].b[x1][y1] = NULL;
				gs->board[player].b[x2][y2] = NULL;
				gs->board[player].b[x1+1][y1] = p1;
				gs->board[player].b[x2+1][y2] = p2;
			}
		}
	}
	else if( dir == DOWN) {

		if(p1->falling == false || p2->falling == false )
			return;

		if((unsigned) p1->y < gs->board[player].height_in_pieces-1 &&
		   (unsigned) p2->y < gs->board[player].height_in_pieces-1 &&
		   ((gs->board[player].b[x1][y1+1] == NULL && relation == UP)  ||
		    (gs->board[player].b[x2][y2+1] == NULL && relation == DOWN)||
		    (gs->board[player].b[x1][y1+1] == NULL && gs->board[player].b[x2][y2+1] == NULL)))
		{
			p1->y++;
			p2->y++;

			gs->board[player].b[x1][y1] = NULL;
			gs->board[player].b[x2][y2] = NULL;
			gs->board[player].b[x1][y1+1] = p1;
			gs->board[player].b[x2][y2+1] = p2;
		} else {
			if(gs->active_couple[player])
				delete gs->active_couple[player];

			gs->active_couple[player] = NULL;

			do{
				SetAllFalling(gs,player);
				FallPieces(gs,player);
			} while(CheckForCombos(gs,player));

			SetAllFalling(gs,player);
			FallPieces(gs,player);
			
			if(gs->board[player].ojamms_pending >= 0){
				OjammAttack(gs,player);
			}
			gs->board[player].last_forced_move = SDL_GetTicks();
		}
	}
	else if( dir == ROTATE )
	{
		/* Second piece always gets rotated around the first piece.
		 * Right becomes up becomes left becomes down becomes right. */
		if(relation == RIGHT){
			if(p1->y != 0){
				p2->x = x1;
				p2->y = y2-1;
				gs->board[player].b[x1][y2-1] = p2;
				gs->board[player].b[x2][y2] = NULL;
			}
		}
		else if(relation == UP){
			if(p1->x > 0 &&
			   gs->board[player].b[x1-1][y1] == NULL){
				p2->x = x1-1;
				p2->y = y1;
				gs->board[player].b[x1-1][y1] = p2;
				gs->board[player].b[x2][y2] = NULL;
			}
		}
		else if(relation == LEFT){
			if(p1->y < gs->board[player].height_in_pieces-1 &&
			   gs->board[player].b[x1][y1+1] == NULL)
			{
				p2->x = x1;
				p2->y = y1+1;
				gs->board[player].b[x1][y1+1] = p2;
				gs->board[player].b[x2][y2] = NULL;
			}
		}
		else if(relation == DOWN){
			if(p1->x < gs->board[player].width_in_pieces - 1 &&
			   gs->board[player].b[x1+1][y1] == NULL)
			{
				p2->x = x1+1;
				p2->y = y1;
				gs->board[player].b[x1+1][y1] = p2;
				gs->board[player].b[x2][y2] = NULL;
			}
		}
	}
}

void SetAllFalling(GameState *gs, int player)
{
	for(unsigned x = 0; x < gs->board[player].width_in_pieces; x++){
		for(unsigned y = 0; y < gs->board[player].height_in_pieces; y++){
			Piece *p = gs->board[player].b[x][y];
			if(p != NULL)
				p->falling = true;
		}
	}
}

void FallPieces(GameState *gs, int player)
{
	SetAllFalling(gs,player);
	for(unsigned x = 0; x < gs->board[player].width_in_pieces; x++){
		for(unsigned y = 0; y < gs->board[player].height_in_pieces; y++){
			Piece *p = gs->board[player].b[x][y];
			if(p != NULL &&
			   p->falling)
			{
				if(gs->active_couple[player] != NULL)
				{
					if((gs->active_couple[player]->p[0]->x == x &&
					    gs->active_couple[player]->p[0]->y == y ) ||
					   (gs->active_couple[player]->p[1]->x == x &&
					    gs->active_couple[player]->p[1]->y == y))
						continue;
				}

				if(p->y != gs->board[player].height_in_pieces-1 &&
				   gs->board[player].b[x][y+1] == NULL){
					p->y++;
					gs->board[player].b[x][y] = NULL;
					gs->board[player].b[x][y+1] = p;
				} else {
					p->falling = false;
				}
			}
		}
	}
}


int getnext(int val, int max){
	if(val+1 == max)
		return 0;
	else
		return val+1;
}

void OjammAttack(GameState *gs, int target)
{
	if(gs->board[target].lost){
		gs->board[getnext(target,gs->player_count)].ojamms_pending += gs->board[target].ojamms_pending;
		gs->board[target].ojamms_pending = 0;
		return;
	}

	if(gs->board[target].ojamms_pending > gs->board[target].width_in_pieces)
		gs->board[target].ojamms_pending = gs->board[target].width_in_pieces;

	int offsetx = rand()%5;
	for(unsigned o = 0; o < gs->board[target].ojamms_pending; o++){
		Piece *ojamm = new Piece();
		ojamm->color = OJAMM;
		ojamm->falling = true;
		ojamm->x = (offsetx + o) % gs->board[target].width_in_pieces;
		ojamm->y = 0;
		ojamm->w = gs->board[target].piece_width;
		ojamm->h = gs->board[target].piece_height;

		gs->board[target].b[ojamm->x][0] = ojamm;
		FallPieces(gs,target);		
	}

	gs->board[target].ojamms_pending = 0;
	FallPieces(gs,target);
}

bool CheckForCombos(GameState *gs, int player)
{
	bool found = false;

	for(unsigned x = 0; x < gs->board[player].width_in_pieces; x++){
		for(unsigned y = 0; y < gs->board[player].height_in_pieces; y++){
			Piece *piece = gs->board[player].b[x][y];
			if(piece == NULL)
				continue;

			std::vector<Piece*> involved;
			std::vector<Piece*> touched;

			BranchSearch(gs, player, x, y, piece->color, involved, touched);

			int ojamms_amongst_them = 0;
			if(involved.size() >= 4) // C-C-C-C-COMBO!
			{
				for(unsigned o_search = 0; o_search < involved.size(); o_search++)
				{
					if(involved[o_search]->color == OJAMM)
						ojamms_amongst_them++;
				}

				if(involved.size() - ojamms_amongst_them < 4)
					goto no_chain;

				for(unsigned i = 0; i < involved.size(); i++){
					if(involved[i]){
						int x1 = involved[i]->x;
						int y1 = involved[i]->y;
						
						/* Generate particles. */
						int particle_count = 4;
						for(unsigned pi = 0; pi < particle_count; pi++)
						{
							int px = gs->board[player].x_offset + (x1 * gs->board[player].piece_width) + (player * gs->board[player].width_in_px);
							int py = gs->board[player].y_offset + (y1 * gs->board[player].piece_height);
							int pxvel = rand()%15+5 * (rand()%2) * -1;
							int pyvel = rand()%15+5 * (rand()%2) * -1;
							Particle pc = {0xFFFFFFFF, SDL_GetTicks(), 500, px, py, pxvel, pyvel};
							gs->particles.push_back(pc);
						}

						delete involved[i];
						gs->board[player].b[x1][y1] = NULL;
					}
				}

				int ojamms = 0;
				switch(involved.size()){
				case 4:
					ojamms = 1;
					break;
				case 5:
					ojamms = 3;
					break;
				case 6:
					ojamms = 5;
					break;
				case 7:
					ojamms = 6;
					break;
				default:
					ojamms = 1;
					break;
				}

				/* OJAMMS, AHOY! */
				if(gs->board[player].ojamms_pending - ojamms <= 0)
					gs->board[player].ojamms_pending = 0;
				else
					gs->board[player].ojamms_pending -= ojamms;
					
				gs->board[getnext(player,gs->player_count)].ojamms_pending += ojamms;

				if(mixer_on)
					Mix_PlayChannel(-1, gs->chain, 0);
				
				found = true;
			}

			no_chain:;
		}
	}

	return found;
}

void BranchSearch(GameState *gs, int player, int x, int y, PieceColor color, std::vector<Piece*> &involved, std::vector<Piece*> &touched)
{

	if(x<0 || y<0 || y >= gs->board[player].height_in_pieces || x >= gs->board[player].width_in_pieces)
		return;

	Piece *p = gs->board[player].b[x][y];
	if(p == NULL)
		return;

	if(std::find(touched.begin(), touched.end(), p) != touched.end())
		return;
	else
		touched.push_back(p);

	if(p->color == color) {
		involved.push_back(p);
	}
	else if(p->color == OJAMM){
		involved.push_back(p);
		return;
	}
	else{
		return;
	}

	BranchSearch(gs,player,x,y-1,color,involved,touched);
	BranchSearch(gs,player,x-1,y,color,involved,touched);
	BranchSearch(gs,player,x+1,y,color,involved,touched);
	BranchSearch(gs,player,x,y+1,color,involved,touched);

	return;
}

void UpdateParticles(std::vector<Particle> &particles)
{
	for(unsigned i = 0; i < particles.size(); i++)
	{
		if(SDL_GetTicks() - particles[i].created_at > particles[i].time_to_live)
		{
			particles.erase(particles.begin()+i);
		} else{
			particles[i].x += particles[i].x_vel;
			particles[i].y += particles[i].y_vel;
		}
	}
}

void CPUTick(GameState *gs, int player)
{
	if( gs->player_types[player] == CPU )
	{
		Direction rnd_dir = (Direction) (rand()%5);
		MoveActiveCouple(gs,player,rnd_dir);
	}
}

// Render ////////////////////////////////////////////////
//////////////////////////////////////////////////////////


void RenderTick(SDL_Surface *screen, GameState *gs)
{
	static const Uint32 bg_color = 0x666666;

	ClearSurfaceTo(screen, bg_color);
	DrawBoardGrids(screen, gs);
	DrawPuyos(screen, gs);
	DrawParticles(screen, gs->particles);
	DrawImpendingDoom(screen, gs);
	DrawWinnerBanner(screen, gs);
	DrawLoserBanner(screen, gs);
}

void ClearSurfaceTo(SDL_Surface *surface, Uint32 color)
{
	SDL_Rect clear = {0,0,surface->w,surface->h};
	SDL_FillRect(surface, &clear, color);
}


void DrawBoardGrids(SDL_Surface *screen, GameState *gs)
{
	for(unsigned p = 0; p < gs->max_players; p++)
	{
	    Sint16 x1 = gs->board[p].x_offset + (gs->board[p].width_in_px * p);
	    Sint16 y1 = gs->board[p].y_offset;
	    Sint16 x2 = x1 + gs->board[p].width_in_px;
	    Sint16 y2 = y1 + gs->board[p].height_in_px;

		if(p >= gs->player_count){
			roundedBoxColor(screen, x1, y1, x2, y2, 2, 0x000000AA);
			roundedRectangleColor(screen, x1, y1, x2, y2, 5, 0x000000FF);
		} else {
			roundedBoxColor(screen, x1, y1, x2, y2, 2, 0x000000AA);
			roundedRectangleColor(screen, x1, y1, x2, y2, 5, 0x000000FF);
		}
	}
}

void DrawPuyos(SDL_Surface *screen, GameState *gs)
{
	for(unsigned p = 0; p < gs->player_count; p++){
		Sint16 x_offset = gs->board[p].x_offset;
		Sint16 y_offset = gs->board[p].y_offset;
		Sint16 width_in_px = gs->board[p].width_in_px;
		Sint16 piece_width = gs->board[p].piece_width;
		Sint16 piece_height = gs->board[p].piece_height;

		for(unsigned x = 0; x < gs->board[p].width_in_pieces; x++){
			for(unsigned y = 0; y < gs->board[p].height_in_pieces; y++){
				if(gs->board[p].b[x][y] != NULL){
					Piece *piece = gs->board[p].b[x][y];
					Sint16 x = x_offset + (piece->x * piece_width)  + (p * width_in_px);
					Sint16 y = y_offset + (piece->y * piece_height);
					Sint16 r = (piece_width + piece_height) / 4;

					Uint32 color = 0x000000FF;
					switch(piece->color)
					{
					case BLUE:
						color = 0x0000FFFF;
						break;
					case ORANGE:
						color = 0xFF9900FF;
						break;
					case GREEN:
						color = 0x00FF00FF;
						break;
					case PURPLE:
						color = 0x9900FFFF;
						break;
					case YELLOW:
						color = 0xFFFF00FF;
						break;
					case OJAMM:
						color = 0x333333FF;
						break;
					default:
						break;
					}

					filledCircleColor(screen, x+r, y+r, r, color);
					filledCircleColor(screen, x+r-r/2, y+r+r/4, r/4, 0x000000FF);
					filledCircleColor(screen, x+r+r/2, y+r+r/4, r/4, 0x000000FF);
				}
			}
		}
	}
}

void DrawParticles(SDL_Surface *screen, std::vector<Particle> &particles)
{
	for(unsigned i = 0; i < particles.size(); i++){
		filledCircleColor(screen, particles[i].x, particles[i].y, 3, particles[i].color);
	}
}

void DrawImpendingDoom(SDL_Surface *screen, GameState *gs)
{
	for(unsigned p = 0; p < gs->player_count; p++)
	{
		if(gs->board[p].ojamms_pending >0 ){
			Sint16 x_offset = gs->board[p].x_offset;
			Sint16 y_offset = gs->board[p].y_offset;
			Sint16 width_in_px = gs->board[p].width_in_px;
			Sint16 piece_width = gs->board[p].piece_width;
			Sint16 piece_height = gs->board[p].piece_height;
		
			int x = x_offset + 15 + (p * width_in_px);
			int y = 5;
			int r = (piece_width + piece_height) / 4;

			Uint32 color = 0x333333FF;
			filledCircleColor(screen, x+r, y+r, r, color);
			filledCircleColor(screen, x+r-r/2, y+r+r/4, r/4, 0x000000FF);
			filledCircleColor(screen, x+r+r/2, y+r+r/4, r/4, 0x000000FF);

			if(font_on){
				std::stringstream s;
				s << " incoming: " << gs->board[p].ojamms_pending << "   ";
				SDL_Color fg = {0xFF,0xFF,0xFF};
				SDL_Color bg = {0x33,0x33,0x33};
				SDL_Surface *fontsurf = TTF_RenderText(gs->font, s.str().c_str(),fg, bg);
				SDL_Rect fontblitrect = {x+r*4, y+2, 50, 50};
				SDL_BlitSurface(fontsurf, NULL, screen, &fontblitrect);
				SDL_FreeSurface(fontsurf);
			}
		}
	}
}

void DrawLoserBanner(SDL_Surface *screen, GameState *gs)
{
	for(unsigned p = 0; p < gs->player_count; p++)
	{
		if(gs->board[p].lost){
			Sint16 x_offset = gs->board[p].x_offset;
			Sint16 y_offset = gs->board[p].y_offset;
			Sint16 width_in_px = gs->board[p].width_in_px;
			Sint16 height_in_px = gs->board[p].height_in_px;
			Sint16 piece_width = gs->board[p].piece_width;
			Sint16 piece_height = gs->board[p].piece_height;

			int bh = height_in_px / 5;
			int bw = width_in_px;
			int bx = x_offset + (p * width_in_px);
			int by = height_in_px / 2;

			std::string loser =  "        You lose.     ";

			SDL_Color fg = {0xFF,0xFF,0xFF};
			SDL_Color bg = {0x33,0x33,0x33};
			SDL_Surface *fontsurf = TTF_RenderText(gs->font, loser.c_str(), fg, bg);
			SDL_Rect fontblitrect = {bx + 20, bh + 10, bw, bh};
			SDL_BlitSurface(fontsurf, NULL, screen, &fontblitrect);
			SDL_FreeSurface(fontsurf);
		}
	}
}


void DrawWinnerBanner(SDL_Surface *screen, GameState *gs)
{
	for(unsigned p = 0; p < gs->player_count; p++)
	{
		if(gs->board[p].won){
			Sint16 x_offset = gs->board[p].x_offset;
			Sint16 y_offset = gs->board[p].y_offset;
			Sint16 width_in_px = gs->board[p].width_in_px;
			Sint16 height_in_px = gs->board[p].height_in_px;
			Sint16 piece_width = gs->board[p].piece_width;
			Sint16 piece_height = gs->board[p].piece_height;

			int bh = height_in_px / 5;
			int bw = width_in_px;
			int bx = x_offset + (p * width_in_px);
			int by = height_in_px / 2;

			std::string loser =  "        You win!     ";

			SDL_Color fg = {0xFF,0xFF,0xFF};
			SDL_Color bg = {0x33,0x33,0x33};
			SDL_Surface *fontsurf = TTF_RenderText(gs->font, loser.c_str(), fg, bg);
			SDL_Rect fontblitrect = {bx + 20, bh + 10, bw, bh};
			SDL_BlitSurface(fontsurf, NULL, screen, &fontblitrect);
			SDL_FreeSurface(fontsurf);
		}
	}
}

// Gamestate /////////////////////////////////////////////
//////////////////////////////////////////////////////////

GameState *InitNewGame()
{
	GameState *newgame = new GameState();
	newgame->playing = true;
	newgame->paused = false;

	for(unsigned p = 0; p < newgame->player_count; p++){
		for(unsigned x = 0; x < newgame->board[p].width_in_pieces; x++){
			for(unsigned y = 0; y < newgame->board[p].height_in_pieces; y++){
				newgame->board[p].b[x][y] = NULL;
			}
		}

		newgame->board[p].lost = false;
		newgame->board[p].won = false;
		newgame->board[p].score = 0;
		newgame->board[p].ojamms_pending = 0;
		newgame->active_couple[p] = NULL;
		newgame->board[p].move_delay = 125;
		newgame->board[p].last_forced_move = SDL_GetTicks();
		newgame->board[p].last_guided_move = SDL_GetTicks();
	}

	if(font_on){
		newgame->font = TTF_OpenFont("eartm.ttf", 24);

		if(newgame->font == NULL)
		{
			font_on = false;
			std::cerr << "Could not load eartm.ttf\n";
		}
	}

	if(mixer_on){
		newgame->bg_mus = Mix_LoadMUS("Black Market VIP.mp3");\
		newgame->chain = Mix_LoadWAV("chain.wav");

		if( newgame->bg_mus == NULL){
			std::cerr << "Could not open pwrglove.mp3! :(\n";
		} else
		{
			Mix_PlayMusic(newgame->bg_mus, -1);
		}

		if(newgame->chain == NULL){
			std::cerr << "Error opening chain.wav, no sound effects!\n";
			mixer_on = false;
		}
	}

	/* Todo: select screen */
	newgame->player_types[0] = HUM;
	newgame->player_types[1] = CPU;
	newgame->player_types[2] = CPU;
	newgame->player_types[3] = CPU;

	// newgame->player_types[2] = CPU2;
	// newgame->player_types[3] = CPU3;

	return newgame;
}



void CleanGameState(GameState *gs)
{
	if(gs){
		for(unsigned p = 0; p < gs->player_count; p++){
//			if(gs->active_couple[p]){
//				delete gs->active_couple[p];
//			}

			for(unsigned x = 0; x < gs->board[p].width_in_pieces; x++){
				for(unsigned y = 0; y < gs->board[p].height_in_pieces; y++){
					if(gs->board[p].b[x][y])
						delete gs->board[p].b[x][y];
				}
			}
		}

		if(font_on){
			if(gs->font){
				TTF_CloseFont(gs->font);
			}
		}

		if(mixer_on){
			if(gs->bg_mus){
				Mix_FreeMusic(gs->bg_mus);
			}
			if(gs->chain){
				Mix_FreeChunk(gs->chain);
			}
		}

		delete gs;
	}
}

// Input /////////////////////////////////////////////////
//////////////////////////////////////////////////////////


void HandleInput(GameState *gs, SDL_Event &event)
{
	/* Player One Controls: asdw */
	if(gs->human_players > 0){
		switch(event.key.keysym.sym){
		case SDLK_w:
			MoveActiveCouple(gs, 0, ROTATE);
			break;
		default:
			break;
		}
	}

	/* Player Two Controls: ghjy */
	if(gs->human_players > 1){
		switch(event.key.keysym.sym){
		case SDLK_y:
			MoveActiveCouple(gs, 1, ROTATE);
			break;
		default:
			break;
		}
	}

	/* Player Three Controls: l;'p */
	if(gs->human_players > 2){
		switch(event.key.keysym.sym){
		case SDLK_p:
			MoveActiveCouple(gs, 2, ROTATE);
			break;
		default:
			break;
		}
	}

	/* Player Three Controls: arrow_keys */
	if(gs->human_players > 3){
		switch(event.key.keysym.sym){
		case SDLK_UP:
			MoveActiveCouple(gs, 3, ROTATE);
			break;
		default:
			break;
		}
	}
}