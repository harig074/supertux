#include "sector.h"

#include <memory>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include "lispreader.h"

#include "badguy.h"
#include "special.h"
#include "gameobjs.h"
#include "camera.h"
#include "background.h"
#include "particlesystem.h"
#include "tile.h"
#include "tilemap.h"
#include "music_manager.h"
#include "gameloop.h"
#include "resources.h"

Sector* Sector::_current = 0;

Sector::Sector()
  : gravity(10), player(0), solids(0), background(0), camera(0),
    currentmusic(LEVEL_MUSIC)
{
  song_title = "Mortimers_chipdisko.mod";
  player = new Player();
  add_object(player);
}

Sector::~Sector()
{
  for(GameObjects::iterator i = gameobjects.begin(); i != gameobjects.end();
      ++i)
    delete *i;

  for(SpawnPoints::iterator i = spawnpoints.begin(); i != spawnpoints.end();
      ++i)
    delete *i;
    
  if(_current == this)
    _current = 0;
}

void
Sector::parse(LispReader& lispreader)
{
  _current = this;
  
  for(lisp_object_t* cur = lispreader.get_lisp(); !lisp_nil_p(cur);
      cur = lisp_cdr(cur)) {
    std::string token = lisp_symbol(lisp_car(lisp_car(cur)));
    lisp_object_t* data = lisp_car(lisp_cdr(lisp_car(cur)));
    LispReader reader(lisp_cdr(lisp_car(cur)));

    if(token == "name") {
      name = lisp_string(data);
    } else if(token == "gravity") {
      gravity = lisp_integer(data);
    } else if(token == "music") {
      song_title = lisp_string(data);
      load_music();
    } else if(token == "camera") {
      if(camera) {
        std::cerr << "Warning: More than 1 camera defined in sector.\n";
        continue;
      }
      camera = new Camera(this);
      camera->read(reader);
      add_object(camera);
    } else if(token == "background") {
      background = new Background(reader);
      add_object(background);
    } else if(token == "playerspawn") {
      SpawnPoint* sp = new SpawnPoint;
      reader.read_string("name", sp->name);
      reader.read_float("x", sp->pos.x);
      reader.read_float("y", sp->pos.y);
    } else if(token == "tilemap") {
      TileMap* tilemap = new TileMap(reader);
      add_object(tilemap);

      if(tilemap->is_solid()) {
        if(solids) {
          std::cerr << "Warning multiple solid tilemaps in sector.\n";
          continue;
        }
        solids = tilemap;
      }
    } else if(badguykind_from_string(token) != BAD_INVALID) {
      add_object(new BadGuy(badguykind_from_string(token), reader));
    } else if(token == "trampoline") {
      add_object(new Trampoline(reader));
    } else if(token == "flying-platform") {
      add_object(new FlyingPlatform(reader));
    } else if(token == "particles-snow") {
      SnowParticleSystem* partsys = new SnowParticleSystem();
      partsys->parse(reader);
      add_object(partsys);
    } else if(token == "particles-clouds") {
      CloudParticleSystem* partsys = new CloudParticleSystem();
      partsys->parse(reader);
      add_object(partsys);
    }
  }

  if(!camera) {
    std::cerr << "sector does not contain a camera.\n";
    camera = new Camera(this);
  }
  if(!solids)
    throw std::runtime_error("sector does not contain a solid tile layer.");
}

void
Sector::parse_old_format(LispReader& reader)
{
  _current = this;
  
  name = "main";
  reader.read_float("gravity", gravity);

  std::string backgroundimage;
  reader.read_string("background", backgroundimage);
  float bgspeed = .5;
  reader.read_float("bkgd_speed", bgspeed);

  Color bkgd_top, bkgd_bottom;
  int r = 0, g = 0, b = 128;
  reader.read_int("bkgd_red_top", r);
  reader.read_int("bkgd_green_top",  g);
  reader.read_int("bkgd_blue_top",  b);
  bkgd_top.red = r;
  bkgd_top.green = g;
  bkgd_top.blue = b;
  
  reader.read_int("bkgd_red_bottom",  r);
  reader.read_int("bkgd_green_bottom", g);
  reader.read_int("bkgd_blue_bottom", b);
  bkgd_bottom.red = r;
  bkgd_bottom.green = g;
  bkgd_bottom.blue = b;
  
  if(backgroundimage != "") {
    background = new Background;
    background->set_image(backgroundimage, bgspeed);
    add_object(background);
  } else {
    background = new Background;
    background->set_gradient(bkgd_top, bkgd_bottom);
    add_object(background);
  }

  std::string particlesystem;
  reader.read_string("particle_system", particlesystem);
  if(particlesystem == "clouds")
    add_object(new CloudParticleSystem());
  else if(particlesystem == "snow")
    add_object(new SnowParticleSystem());

  Vector startpos(100, 170);
  reader.read_float("start_pos_x", startpos.x);
  reader.read_float("start_pos_y", startpos.y);

  SpawnPoint* spawn = new SpawnPoint;
  spawn->pos = startpos;
  spawn->name = "main";
  spawnpoints.push_back(spawn);

  song_title = "Mortimers_chipdisko.mod";
  reader.read_string("music", song_title);
  load_music();

  int width, height = 15;
  reader.read_int("width", width);
  reader.read_int("height", height);
  
  std::vector<unsigned int> tiles;
  if(reader.read_int_vector("interactive-tm", tiles)
      || reader.read_int_vector("tilemap", tiles)) {
    TileMap* tilemap = new TileMap();
    tilemap->set(width, height, tiles, LAYER_TILES, true);
    solids = tilemap;
    add_object(tilemap);
  }

  if(reader.read_int_vector("background-tm", tiles)) {
    TileMap* tilemap = new TileMap();
    tilemap->set(width, height, tiles, LAYER_BACKGROUNDTILES, false);
    add_object(tilemap);
  }

  if(reader.read_int_vector("foreground-tm", tiles)) {
    TileMap* tilemap = new TileMap();
    tilemap->set(width, height, tiles, LAYER_FOREGROUNDTILES, false);
    add_object(tilemap);
  }

  // TODO read resetpoints

  // read objects
  {
    lisp_object_t* cur = 0;
    if(reader.read_lisp("objects", cur)) {
      while(!lisp_nil_p(cur)) {
        lisp_object_t* data = lisp_car(cur);
        std::string object_type = lisp_symbol(lisp_car(data));
                                                                                
        LispReader reader(lisp_cdr(data));
                                                                                
        if(object_type == "trampoline") {
          add_object(new Trampoline(reader));
        }
        else if(object_type == "flying-platform") {
          add_object(new FlyingPlatform(reader));
        }
        else {
          BadGuyKind kind = badguykind_from_string(object_type);
          add_object(new BadGuy(kind, reader));
        }
                                                                                
        cur = lisp_cdr(cur);
      }
    }
  }

  // add a camera
  camera = new Camera(this);
  add_object(camera);
}

void
Sector::write(LispWriter& writer)
{
  writer.write_string("name", name);
  writer.write_float("gravity", gravity);

  for(GameObjects::iterator i = gameobjects.begin();
      i != gameobjects.end(); ++i) {
    Serializable* serializable = dynamic_cast<Serializable*> (*i);
    if(serializable)
      serializable->write(writer);
  }
}

void
Sector::add_object(GameObject* object)
{
  // XXX a bit hackish, at least try to keep the number of these things down...
  BadGuy* badguy = dynamic_cast<BadGuy*> (object);
  if(badguy)
    badguys.push_back(badguy);
  Bullet* bullet = dynamic_cast<Bullet*> (object);
  if(bullet)
    bullets.push_back(bullet);
  Upgrade* upgrade = dynamic_cast<Upgrade*> (object);
  if(upgrade)
    upgrades.push_back(upgrade);
  Trampoline* trampoline = dynamic_cast<Trampoline*> (object);
  if(trampoline)
    trampolines.push_back(trampoline);
  FlyingPlatform* flying_platform = dynamic_cast<FlyingPlatform*> (object);
  if(flying_platform)
    flying_platforms.push_back(flying_platform);
  Background* background = dynamic_cast<Background*> (object);
  if(background)
    this->background = background;

  gameobjects.push_back(object);
}

void
Sector::activate(const std::string& spawnpoint)
{
  _current = this;

  // Apply bonuses from former levels
  switch (player_status.bonus)
    {
    case PlayerStatus::NO_BONUS:
      break;
                                                                                
    case PlayerStatus::FLOWER_BONUS:
      player->got_power = Player::FIRE_POWER;  // FIXME: add ice power to here
      // fall through
                                                                                
    case PlayerStatus::GROWUP_BONUS:
      player->grow(false);
      break;
    }

  SpawnPoint* sp = 0;
  for(SpawnPoints::iterator i = spawnpoints.begin(); i != spawnpoints.end();
      ++i) {
    if((*i)->name == spawnpoint) {
      sp = *i;
      break;
    }
  }
  if(!sp) {
    std::cerr << "Spawnpoint '" << spawnpoint << "' not found.\n";
  } else {
    player->move(sp->pos);
  }

  camera->reset(Vector(player->base.x, player->base.y));
}

void
Sector::action(float elapsed_time)
{
  player->check_bounds(camera);
                                                                                
  /* update objects (don't use iterators here, because the list might change
   * during the iteration)
   */
  for(size_t i = 0; i < gameobjects.size(); ++i)
    if(gameobjects[i]->is_valid())
      gameobjects[i]->action(elapsed_time);
                                                                                
  /* Handle all possible collisions. */
  collision_handler();
                                                                                
  /** cleanup marked objects */
  for(std::vector<GameObject*>::iterator i = gameobjects.begin();
      i != gameobjects.end(); /* nothing */) {
    if((*i)->is_valid() == false) {
      BadGuy* badguy = dynamic_cast<BadGuy*> (*i);
      if(badguy) {
        badguys.erase(std::remove(badguys.begin(), badguys.end(), badguy),
            badguys.end());
      }
      Bullet* bullet = dynamic_cast<Bullet*> (*i);
      if(bullet) {
        bullets.erase(
            std::remove(bullets.begin(), bullets.end(), bullet),
            bullets.end());
      }
      Upgrade* upgrade = dynamic_cast<Upgrade*> (*i);
      if(upgrade) {
        upgrades.erase(
            std::remove(upgrades.begin(), upgrades.end(), upgrade),
            upgrades.end());
      }
      Trampoline* trampoline = dynamic_cast<Trampoline*> (*i);
      if(trampoline) {
        trampolines.erase(
            std::remove(trampolines.begin(), trampolines.end(), trampoline),
            trampolines.end());
      }
      FlyingPlatform* flying_platform= dynamic_cast<FlyingPlatform*> (*i);
      if(flying_platform) {
        flying_platforms.erase(
            std::remove(flying_platforms.begin(), flying_platforms.end(), flying_platform),
            flying_platforms.end());
      }
                                                                                
      delete *i;
      i = gameobjects.erase(i);
    } else {
      ++i;
    }
  }
}

void
Sector::draw(DrawingContext& context)
{
  context.push_transform();
  context.set_translation(camera->get_translation());
  
  for(GameObjects::iterator i = gameobjects.begin();
      i != gameobjects.end(); ++i) {
    if( (*i)->is_valid() )
      (*i)->draw(context);
  }

  context.pop_transform();
}

void
Sector::collision_handler()
{
  // CO_BULLET & CO_BADGUY check
  for(unsigned int i = 0; i < bullets.size(); ++i)
    {
      for (BadGuys::iterator j = badguys.begin(); j != badguys.end(); ++j)
        {
          if((*j)->dying != DYING_NOT)
            continue;
                                                                                
          if(rectcollision(bullets[i]->base, (*j)->base))
            {
              // We have detected a collision and now call the
              // collision functions of the collided objects.
              (*j)->collision(bullets[i], CO_BULLET, COLLISION_NORMAL);
              bullets[i]->collision(CO_BADGUY);
              break; // bullet is invalid now, so break
            }
        }
    }
                                                                                
  /* CO_BADGUY & CO_BADGUY check */
  for (BadGuys::iterator i = badguys.begin(); i != badguys.end(); ++i)
    {
      if((*i)->dying != DYING_NOT)
        continue;
                                                                                
      BadGuys::iterator j = i;
      ++j;
      for (; j != badguys.end(); ++j)
        {
          if(j == i || (*j)->dying != DYING_NOT)
            continue;
                                                                                
          if(rectcollision((*i)->base, (*j)->base))
            {
              // We have detected a collision and now call the
              // collision functions of the collided objects.
              (*j)->collision(*i, CO_BADGUY);
              (*i)->collision(*j, CO_BADGUY);
            }
        }
    }
  if(player->dying != DYING_NOT) return;
                                                                                
  // CO_BADGUY & CO_PLAYER check
  for (BadGuys::iterator i = badguys.begin(); i != badguys.end(); ++i)
    {
      if((*i)->dying != DYING_NOT)
        continue;
                                                                                
      if(rectcollision_offset((*i)->base, player->base, 0, 0))
        {
          // We have detected a collision and now call the collision
          // functions of the collided objects.
          if (player->previous_base.y < player->base.y &&
              player->previous_base.y + player->previous_base.height
              < (*i)->base.y + (*i)->base.height/2
              && !player->invincible_timer.started())
            {
              (*i)->collision(player, CO_PLAYER, COLLISION_SQUISH);
            }
          else
            {
              player->collision(*i, CO_BADGUY);
              (*i)->collision(player, CO_PLAYER, COLLISION_NORMAL);
            }
        }
    }
                                                                                
  // CO_UPGRADE & CO_PLAYER check
  for(unsigned int i = 0; i < upgrades.size(); ++i)
    {
      if(rectcollision(upgrades[i]->base, player->base))
        {
          // We have detected a collision and now call the collision
          // functions of the collided objects.
          upgrades[i]->collision(player, CO_PLAYER, COLLISION_NORMAL);
        }
    }
                                                                                
  // CO_TRAMPOLINE & (CO_PLAYER or CO_BADGUY)
  for (Trampolines::iterator i = trampolines.begin(); i != trampolines.end(); ++i)
  {
    if (rectcollision((*i)->base, player->base))
    {
      if (player->previous_base.y < player->base.y &&
          player->previous_base.y + player->previous_base.height
          < (*i)->base.y + (*i)->base.height/2)
      {
        (*i)->collision(player, CO_PLAYER, COLLISION_SQUISH);
      }
      else if (player->previous_base.y <= player->base.y)
      {
        player->collision(*i, CO_TRAMPOLINE);
        (*i)->collision(player, CO_PLAYER, COLLISION_NORMAL);
      }
    }
  }
                                                                                
  // CO_FLYING_PLATFORM & (CO_PLAYER or CO_BADGUY)
  for (FlyingPlatforms::iterator i = flying_platforms.begin(); i != flying_platforms.end(); ++i)
  {
    if (rectcollision((*i)->base, player->base))
    {
      if (player->previous_base.y < player->base.y &&
          player->previous_base.y + player->previous_base.height
          < (*i)->base.y + (*i)->base.height/2)
      {
        (*i)->collision(player, CO_PLAYER, COLLISION_SQUISH);
        player->collision(*i, CO_FLYING_PLATFORM);
      }
/*      else if (player->previous_base.y <= player->base.y)
      {
      }*/
    }
  }
}

void
Sector::add_score(const Vector& pos, int s)
{
  player_status.score += s;
                                                                                
  add_object(new FloatingScore(pos, s));
}
                                                                                
void
Sector::add_bouncy_distro(const Vector& pos)
{
  add_object(new BouncyDistro(pos));
}
                                                                                
void
Sector::add_broken_brick(const Vector& pos, Tile* tile)
{
  add_broken_brick_piece(pos, Vector(-1, -4), tile);
  add_broken_brick_piece(pos + Vector(0, 16), Vector(-1.5, -3), tile);
                                                                                
  add_broken_brick_piece(pos + Vector(16, 0), Vector(1, -4), tile);
  add_broken_brick_piece(pos + Vector(16, 16), Vector(1.5, -3), tile);
}
                                                                                
void
Sector::add_broken_brick_piece(const Vector& pos, const Vector& movement,
    Tile* tile)
{
  add_object(new BrokenBrick(tile, pos, movement));
}
                                                                                
void
Sector::add_bouncy_brick(const Vector& pos)
{
  add_object(new BouncyBrick(pos));
}

BadGuy*
Sector::add_bad_guy(float x, float y, BadGuyKind kind)
{
  BadGuy* badguy = new BadGuy(kind, x, y);
  add_object(badguy);
  return badguy;
}
                                                                                
void
Sector::add_upgrade(const Vector& pos, Direction dir, UpgradeKind kind)
{
  add_object(new Upgrade(pos, dir, kind));
}
                                                                                
bool
Sector::add_bullet(const Vector& pos, float xm, Direction dir)
{
  if(player->got_power == Player::FIRE_POWER)
    {
    if(bullets.size() > MAX_FIRE_BULLETS-1)
      return false;
    }
  else if(player->got_power == Player::ICE_POWER)
    {
    if(bullets.size() > MAX_ICE_BULLETS-1)
      return false;
    }
                                                                                
  Bullet* new_bullet = 0;
  if(player->got_power == Player::FIRE_POWER)
    new_bullet = new Bullet(pos, xm, dir, FIRE_BULLET);
  else if(player->got_power == Player::ICE_POWER)
    new_bullet = new Bullet(pos, xm, dir, ICE_BULLET);
  else
    throw std::runtime_error("wrong bullet type.");
  add_object(new_bullet);
                                                                                
  play_sound(sounds[SND_SHOOT], SOUND_CENTER_SPEAKER);
                                                                                
  return true;
}

/* Break a brick: */
bool
Sector::trybreakbrick(const Vector& pos, bool small)
{
  Tile* tile = solids->get_tile_at(pos);
  if (tile->attributes & Tile::BRICK)
    {
      if (tile->data > 0)
        {
          /* Get a distro from it: */
          add_bouncy_distro(
              Vector(((int)(pos.x + 1) / 32) * 32, (int)(pos.y / 32) * 32));
                                                                                
          // TODO: don't handle this in a global way but per-tile...
          if (!counting_distros)
            {
              counting_distros = true;
              distro_counter = 5;
            }
          else
            {
              distro_counter--;
            }
                                                                                
          if (distro_counter <= 0)
            {
              counting_distros = false;
              solids->change_at(pos, tile->next_tile);
            }
                                                                                
          play_sound(sounds[SND_DISTRO], SOUND_CENTER_SPEAKER);
          player_status.score = player_status.score + SCORE_DISTRO;
          player_status.distros++;
          return true;
        }
      else if (!small)
        {
          /* Get rid of it: */
          solids->change_at(pos, tile->next_tile);
                                                                                
          /* Replace it with broken bits: */
          add_broken_brick(Vector(
                                 ((int)(pos.x + 1) / 32) * 32,
                                 (int)(pos.y / 32) * 32), tile);
                                                                                
          /* Get some score: */
          play_sound(sounds[SND_BRICK], SOUND_CENTER_SPEAKER);
          player_status.score = player_status.score + SCORE_BRICK;
                                                                                
          return true;
        }
    }
                                                                                
  return false;
}
                                                                                
/* Empty a box: */
void
Sector::tryemptybox(const Vector& pos, Direction col_side)
{
  Tile* tile = solids->get_tile_at(pos);
  if (!(tile->attributes & Tile::FULLBOX))
    return;
                                                                                
  // according to the collision side, set the upgrade direction
  if(col_side == LEFT)
    col_side = RIGHT;
  else
    col_side = LEFT;
                                                                                
  int posx = ((int)(pos.x+1) / 32) * 32;
  int posy = (int)(pos.y/32) * 32 - 32;
  switch(tile->data)
    {
    case 1: // Box with a distro!
      add_bouncy_distro(Vector(posx, posy));
      play_sound(sounds[SND_DISTRO], SOUND_CENTER_SPEAKER);
      player_status.score = player_status.score + SCORE_DISTRO;
      player_status.distros++;
      break;
                                                                                
    case 2: // Add a fire flower upgrade!
      if (player->size == SMALL)     /* Tux is small, add mints! */
        add_upgrade(Vector(posx, posy), col_side, UPGRADE_GROWUP);
      else     /* Tux is big, add a fireflower: */
        add_upgrade(Vector(posx, posy), col_side, UPGRADE_FIREFLOWER);
      play_sound(sounds[SND_UPGRADE], SOUND_CENTER_SPEAKER);
      break;
                                                                                
    case 5: // Add an ice flower upgrade!
      if (player->size == SMALL)     /* Tux is small, add mints! */
        add_upgrade(Vector(posx, posy), col_side, UPGRADE_GROWUP);
      else     /* Tux is big, add an iceflower: */
        add_upgrade(Vector(posx, posy), col_side, UPGRADE_ICEFLOWER);
      play_sound(sounds[SND_UPGRADE], SOUND_CENTER_SPEAKER);
      break;
                                                                                
    case 3: // Add a golden herring
      add_upgrade(Vector(posx, posy), col_side, UPGRADE_HERRING);
      break;
                                                                                
    case 4: // Add a 1up extra
      add_upgrade(Vector(posx, posy), col_side, UPGRADE_1UP);
      break;
    default:
      break;
    }
                                                                                
  /* Empty the box: */
  solids->change_at(pos, tile->next_tile);
}
                                                                                
/* Try to grab a distro: */
void
Sector::trygrabdistro(const Vector& pos, int bounciness)
{
  Tile* tile = solids->get_tile_at(pos);
  if (!(tile->attributes & Tile::COIN))
    return;

  solids->change_at(pos, tile->next_tile);
  play_sound(sounds[SND_DISTRO], SOUND_CENTER_SPEAKER);
                                                                            
  if (bounciness == BOUNCE)
    {
      add_bouncy_distro(Vector(((int)(pos.x + 1) / 32) * 32,
                              (int)(pos.y / 32) * 32));
    }
                                                                            
  player_status.score = player_status.score + SCORE_DISTRO;
  player_status.distros++;
}
                                                                                
/* Try to bump a bad guy from below: */
void
Sector::trybumpbadguy(const Vector& pos)
{
  // Bad guys:
  for (BadGuys::iterator i = badguys.begin(); i != badguys.end(); ++i)
    {
      if ((*i)->base.x >= pos.x - 32 && (*i)->base.x <= pos.x + 32 &&
          (*i)->base.y >= pos.y - 16 && (*i)->base.y <= pos.y + 16)
        {
          (*i)->collision(player, CO_PLAYER, COLLISION_BUMP);
        }
    }
                                                                                
  // Upgrades:
  for (unsigned int i = 0; i < upgrades.size(); i++)
    {
      if (upgrades[i]->base.height == 32 &&
          upgrades[i]->base.x >= pos.x - 32 && upgrades[i]->base.x <= pos.x + 32 &&
          upgrades[i]->base.y >= pos.y - 16 && upgrades[i]->base.y <= pos.y + 16)
        {
          upgrades[i]->collision(player, CO_PLAYER, COLLISION_BUMP);
        }
    }
}

void
Sector::load_music()
{
  char* song_path;
  char* song_subtitle;
                                                                                
  level_song = music_manager->load_music(datadir + "/music/" + song_title);
                                                                                
  song_path = (char *) malloc(sizeof(char) * datadir.length() +
                              strlen(song_title.c_str()) + 8 + 5);
  song_subtitle = strdup(song_title.c_str());
  strcpy(strstr(song_subtitle, "."), "\0");
  sprintf(song_path, "%s/music/%s-fast%s", datadir.c_str(),
          song_subtitle, strstr(song_title.c_str(), "."));
  if(!music_manager->exists_music(song_path)) {
    level_song_fast = level_song;
  } else {
    level_song_fast = music_manager->load_music(song_path);
  }
  free(song_subtitle);
  free(song_path);
}

void
Sector::play_music(int type)
{
  currentmusic = type;
  switch(currentmusic) {
    case HURRYUP_MUSIC:
      music_manager->play_music(level_song_fast);
      break;
    case LEVEL_MUSIC:
      music_manager->play_music(level_song);
      break;
    case HERRING_MUSIC:
      music_manager->play_music(herring_song);
      break;
    default:
      music_manager->halt_music();
      break;
  }
}

int
Sector::get_music_type()
{
  return currentmusic;
}
