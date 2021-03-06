/*
Minetest
Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "player.h"
#include "hud.h"
#include "constants.h"
#include "gamedef.h"
#include "settings.h"
#include "content_sao.h"
#include "util/numeric.h"

Player::Player(IGameDef *gamedef):
	touching_ground(false),
	in_liquid(false),
	in_liquid_stable(false),
	liquid_viscosity(0),
	is_climbing(false),
	swimming_vertical(false),
	camera_barely_in_ceiling(false),
	light(0),
	inventory(gamedef->idef()),
	hp(PLAYER_MAX_HP),
	hurt_tilt_timer(0),
	hurt_tilt_strength(0),
	peer_id(PEER_ID_INEXISTENT),
	keyPressed(0),
// protected
	m_gamedef(gamedef),
	m_breath(-1),
	m_pitch(0),
	m_yaw(0),
	m_speed(0,0,0),
	m_position(0,0,0),
	m_collisionbox(-BS*0.30,0.0,-BS*0.30,BS*0.30,BS*1.55,BS*0.30),
	m_last_pitch(0),
	m_last_yaw(0),
	m_last_pos(0,0,0),
	m_last_hp(PLAYER_MAX_HP),
	m_last_inventory(gamedef->idef())
{
	updateName("<not set>");
	inventory.clear();
	inventory.addList("main", PLAYER_INVENTORY_SIZE);
	InventoryList *craft = inventory.addList("craft", 9);
	craft->setWidth(3);
	inventory.addList("craftpreview", 1);
	inventory.addList("craftresult", 1);
	m_last_inventory = inventory;

	// Can be redefined via Lua
	inventory_formspec = "size[8,7.5]"
		//"image[1,0.6;1,2;player.png]"
		"list[current_player;main;0,3.5;8,4;]"
		"list[current_player;craft;3,0;3,3;]"
		"list[current_player;craftpreview;7,1;1,1;]";

	// Initialize movement settings at default values, so movement can work if the server fails to send them
	movement_acceleration_default   = 3    * BS;
	movement_acceleration_air       = 2    * BS;
	movement_acceleration_fast      = 10   * BS;
	movement_speed_walk             = 4    * BS;
	movement_speed_crouch           = 1.35 * BS;
	movement_speed_fast             = 20   * BS;
	movement_speed_climb            = 2    * BS;
	movement_speed_jump             = 6.5  * BS;
	movement_liquid_fluidity        = 1    * BS;
	movement_liquid_fluidity_smooth = 0.5  * BS;
	movement_liquid_sink            = 10   * BS;
	movement_gravity                = 9.81 * BS;

	// Movement overrides are multipliers and must be 1 by default
	physics_override_speed   = 1;
	physics_override_jump    = 1;
	physics_override_gravity = 1;

	hud_flags = HUD_FLAG_HOTBAR_VISIBLE | HUD_FLAG_HEALTHBAR_VISIBLE |
			 HUD_FLAG_CROSSHAIR_VISIBLE | HUD_FLAG_WIELDITEM_VISIBLE |
			 HUD_FLAG_BREATHBAR_VISIBLE;

	hud_hotbar_itemcount = HUD_HOTBAR_ITEMCOUNT_DEFAULT;
}

Player::~Player()
{
}

// Horizontal acceleration (X and Z), Y direction is ignored
void Player::accelerateHorizontal(v3f target_speed, f32 max_increase)
{
	if(max_increase == 0)
		return;

	v3f d_wanted = target_speed - m_speed;
	d_wanted.Y = 0;
	f32 dl = d_wanted.getLength();
	if(dl > max_increase)
		dl = max_increase;
	
	v3f d = d_wanted.normalize() * dl;

	m_speed.X += d.X;
	m_speed.Z += d.Z;

#if 0 // old code
	if(m_speed.X < target_speed.X - max_increase)
		m_speed.X += max_increase;
	else if(m_speed.X > target_speed.X + max_increase)
		m_speed.X -= max_increase;
	else if(m_speed.X < target_speed.X)
		m_speed.X = target_speed.X;
	else if(m_speed.X > target_speed.X)
		m_speed.X = target_speed.X;

	if(m_speed.Z < target_speed.Z - max_increase)
		m_speed.Z += max_increase;
	else if(m_speed.Z > target_speed.Z + max_increase)
		m_speed.Z -= max_increase;
	else if(m_speed.Z < target_speed.Z)
		m_speed.Z = target_speed.Z;
	else if(m_speed.Z > target_speed.Z)
		m_speed.Z = target_speed.Z;
#endif
}

// Vertical acceleration (Y), X and Z directions are ignored
void Player::accelerateVertical(v3f target_speed, f32 max_increase)
{
	if(max_increase == 0)
		return;

	f32 d_wanted = target_speed.Y - m_speed.Y;
	if(d_wanted > max_increase)
		d_wanted = max_increase;
	else if(d_wanted < -max_increase)
		d_wanted = -max_increase;

	m_speed.Y += d_wanted;

#if 0 // old code
	if(m_speed.Y < target_speed.Y - max_increase)
		m_speed.Y += max_increase;
	else if(m_speed.Y > target_speed.Y + max_increase)
		m_speed.Y -= max_increase;
	else if(m_speed.Y < target_speed.Y)
		m_speed.Y = target_speed.Y;
	else if(m_speed.Y > target_speed.Y)
		m_speed.Y = target_speed.Y;
#endif
}

v3s16 Player::getLightPosition() const
{
	return floatToInt(m_position + v3f(0,BS+BS/2,0), BS);
}

void Player::serialize(std::ostream &os)
{
	// Utilize a Settings object for storing values
	Settings args;
	args.setS32("version", 1);
	args.set("name", m_name);
	//args.set("password", m_password);
	args.setFloat("pitch", m_pitch);
	args.setFloat("yaw", m_yaw);
	args.setV3F("position", m_position);
	args.setS32("hp", hp);
	args.setS32("breath", m_breath);

	args.writeLines(os);

	os<<"PlayerArgsEnd\n";

	inventory.serialize(os);
}

void Player::deSerialize(std::istream &is, std::string playername)
{
	Settings args;
	
	for(;;)
	{
		if(is.eof())
			throw SerializationError
					(("Player::deSerialize(): PlayerArgsEnd of player \"" + playername + "\" not found").c_str());
		std::string line;
		std::getline(is, line);
		std::string trimmedline = trim(line);
		if(trimmedline == "PlayerArgsEnd")
			break;
		args.parseConfigLine(line);
	}

	//args.getS32("version"); // Version field value not used
	std::string name = args.get("name");
	updateName(name.c_str());
	setPitch(args.getFloat("pitch"));
	setYaw(args.getFloat("yaw"));
	setPosition(args.getV3F("position"));
	try{
		hp = args.getS32("hp");
	}catch(SettingNotFoundException &e){
		hp = 20;
	}
	try{
		m_breath = args.getS32("breath");
	}catch(SettingNotFoundException &e){
		m_breath = 11;
	}

	inventory.deSerialize(is);

	if(inventory.getList("craftpreview") == NULL)
	{
		// Convert players without craftpreview
		inventory.addList("craftpreview", 1);

		bool craftresult_is_preview = true;
		if(args.exists("craftresult_is_preview"))
			craftresult_is_preview = args.getBool("craftresult_is_preview");
		if(craftresult_is_preview)
		{
			// Clear craftresult
			inventory.getList("craftresult")->changeItem(0, ItemStack());
		}
	}

	// Set m_last_*
	checkModified();
}

/*
	RemotePlayer
*/





void RemotePlayer::setPosition(const v3f &position)
{
	Player::setPosition(position);
	if(m_sao)
		m_sao->setBasePosition(position);
}
