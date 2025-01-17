#include <algorithm>

#include "cpp_quakedef.hpp"

#include "script_playback.hpp"

#include "hooks.h"

#include "afterframes.hpp"
#include "reset.hpp"
#include "script_parse.hpp"
#include "strafing.hpp"
#include "utils.hpp"
#include "savestate.hpp"
#include "bookmark.hpp"
#include "camera.hpp"

static PlaybackInfo playback;
const int LOWEST_FRAME = 0;
const int LOWEST_BLOCK = 0;
static char BUFFER[256];

enum class MouseState
{
	Locked,
	Strafe,
	Yaw,
	Pitch,
	Mixed,
	Swim
};

static float current_strafeyaw = 0;
static float current_strafepitch = 0;
static float current_yaw = 0;
static float current_pitch = 0;
static int current_strafe = 0;

static vec3_t old_angles;
static MouseState m_state = MouseState::Locked;

// desc: How many backups to keep while saving the script to file.
cvar_t tas_edit_backups = {"tas_edit_backups", "100"};
// desc: How much rounding to apply when setting the strafe yaw and pitch
cvar_t tas_edit_snap_threshold = {"tas_edit_snap_threshold", "0.001"};

static bool Set_Pause_Frame(int pause_frame)
{
	if (playback.Get_Number_Of_Blocks() == 0)
	{
		Con_Print("No script loaded\n");
		return false;
	}
	else if (pause_frame >= 0)
	{
		playback.pause_frame = pause_frame;
	}
	else
	{
		int frames_from_end = -1 - pause_frame;
		auto& last_block = playback.current_script.blocks[playback.current_script.blocks.size() - 1];
		playback.pause_frame = last_block.frame - frames_from_end;
	}

	Savestate_Playback_Started(playback.pause_frame);

	return true;
}


static void Savestate_Skip(int start_frame)
{
	playback.current_frame = start_frame;
	if (playback.current_frame < playback.pause_frame)
	{
		tas_timescale.value = 999999;
		r_norefresh.value = 1;
		AddAfterframes(playback.pause_frame - 1 - playback.current_frame, "tas_timescale 1; r_norefresh 0");
	}

	for (auto& block : playback.current_script.blocks)
	{
		if(block.frame >= playback.current_frame)
			break;

		playback.stacked.Stack(block);
	}

	playback.stacked.commands.clear();
	auto cmd = playback.stacked.GetCommand();
	AddAfterframes(1, const_cast<char*>(cmd.c_str()), NoFilter);
}

static void Normal_Skip()
{
	tas_timescale.value = 999999;
	r_norefresh.value = 1;
	AddAfterframes(playback.pause_frame - 1 - playback.current_frame, "tas_timescale 1; r_norefresh 0");
}

void Run_Script(int frame, bool skip, bool ss)
{
	if(!Set_Pause_Frame(frame))
		return;

	playback.current_frame = 0;
	playback.stacked.Reset();
	Cmd_TAS_Cmd_Reset();
	tas_playing.value = 1;

	if (skip)
	{
		int ss_frame = Savestate_Load_State(frame);
		if (ss_frame < 0 && ss)
		{
			Normal_Skip();
		}
		else
		{
			Savestate_Skip(ss_frame);
		}
	}

	playback.script_running = true;
	playback.last_edited = Sys_DoubleTime();
}

static void Continue_Script(int frames)
{
	if (!Set_Pause_Frame(playback.current_frame + frames))
		return;

	playback.script_running = true;
	playback.should_unpause = true;
}

static void Generic_Advance(int frame)
{
	key_dest = key_game;
	if (frame > playback.current_frame)
		Continue_Script(frame - playback.current_frame);
	else if (frame < LOWEST_FRAME)
	{
		Con_Printf("Cannot skip to frame %d\n", frame);
	}
	else
	{
		Run_Script(frame, true);
	}
}

bool CurrentFrameHasBlock(int frame)
{
	if(frame == -1)
		frame = playback.current_frame;

	for (int i = 0; i < playback.current_script.blocks.size(); ++i)
	{
		if (playback.current_script.blocks[i].frame == frame)
		{
			return true;
		}
	}

	return false;
}

static int AddBlock(int frame)
{
	FrameBlock block;
	block.frame = frame;
	block.parsed = true;

	if (playback.current_script.blocks.empty())
	{
		playback.current_script.blocks.push_back(block);
		return 0;
	}
	else
	{
		int i;

		for (i = 0; i < playback.current_script.blocks.size(); ++i)
		{
			if (playback.current_script.blocks[i].frame > frame)
			{
				playback.current_script.blocks.insert(playback.current_script.blocks.begin() + i,
				                                      block);
				return i;
			}
		}

		playback.current_script.blocks.insert(playback.current_script.blocks.begin() + i, block);
		return i;
	}
}

static FrameBlock* GetBlockForFrame(int frame=-1)
{
	if (playback.current_script.blocks.empty())
	{
		Con_Printf("Cannot add blocks to an empty script!\n");
		return nullptr;
	}

	if (frame == -1)
	{
		frame = playback.current_frame;
	}

	if (CurrentFrameHasBlock(frame))
	{
		return &playback.current_script.blocks[playback.GetBlockNumber(frame)];
	}

	int block = AddBlock(frame);
	return &playback.current_script.blocks[block];
}

static float Get_Stacked_Value(const char* name)
{
	if (playback.stacked.convars.find(name) != playback.stacked.convars.end())
	{
		return playback.stacked.convars[name];
	}
	else
		return Get_Default_Value(name);
}

static float Get_Existing_Value(const char* name)
{
	auto curblock = GetBlockForFrame();

	if (curblock->convars.find(name) != curblock->convars.end())
	{
		return curblock->convars[name];
	}
	else
		return Get_Stacked_Value(name);
}

static bool Has_Existing_Value(const char* name)
{
	auto curblock = GetBlockForFrame();

	return curblock->convars.find(name) != curblock->convars.end() ||
	playback.stacked.convars.find(name) != playback.stacked.convars.end();
}

static bool Get_Stacked_Toggle(const char* cmd_name)
{
	if (playback.stacked.toggles.find(cmd_name) != playback.stacked.toggles.end())
		return playback.stacked.toggles[cmd_name];
	else
		return false;
}

static bool Get_Existing_Toggle(const char* cmd_name)
{
	auto curblock = GetBlockForFrame();

	if (curblock->toggles.find(cmd_name) != curblock->toggles.end())
		return curblock->toggles[cmd_name];
	else
		return Get_Stacked_Toggle(cmd_name);
}

static void SetConvar(const char* name, float new_val, bool silent = false, int frame = -1)
{
	if (frame == -1)
	{
		frame = playback.current_frame;
	}

	float old_val = Get_Existing_Value(name);

	if (new_val == old_val)
	{
		if (!silent)
			Con_Printf("Value identical to old value.\n");
		return;
	}

	playback.last_edited = Sys_DoubleTime();
	FrameBlock* block  = GetBlockForFrame(frame);

	if (!silent)
		CenterPrint("Block: Added %s %f", name, new_val);

	if (Get_Stacked_Value(name) == new_val && block->convars.find(name) != block->convars.end())
		block->convars.erase(name);
	else
		block->convars[name] = new_val;

	Savestate_Script_Updated(frame);
}

void SetToggle(const char* cmd, bool new_value, bool silent = false, int frame = -1)
{
	if (frame == -1)
	{
		frame = playback.current_frame;
	}

	playback.last_edited = Sys_DoubleTime();
	auto block = GetBlockForFrame(frame);
	if(!silent)
		CenterPrint("Block: Added %c%s", new_value ? '+' : '-', cmd);

	if (Get_Stacked_Toggle(cmd) == new_value && block->toggles.find(cmd) != block->toggles.end())
		block->toggles.erase(cmd);
	else
		block->toggles[cmd] = new_value;

	Savestate_Script_Updated(frame);
}

static void ApplyMouseStuff()
{
	if (m_state == MouseState::Strafe)
	{
		float yaw = Round(cl.viewangles[YAW], tas_edit_snap_threshold.value);
		SetConvar("tas_strafe", 1, true);
		SetConvar("tas_strafe_yaw", yaw, true);
	}
	else if (m_state == MouseState::Mixed)
	{
		SetConvar("tas_view_yaw", cl.viewangles[YAW], true);
		SetConvar("tas_view_pitch", cl.viewangles[PITCH], true);
	}
	else if (m_state == MouseState::Pitch)
	{
		SetConvar("tas_view_pitch", cl.viewangles[PITCH], true);
	}
	else if (m_state == MouseState::Yaw)
	{
		SetConvar("tas_view_yaw", cl.viewangles[YAW], true);
	}
	else if (m_state == MouseState::Swim)
	{
		SetConvar("tas_strafe_yaw", cl.viewangles[YAW], true);
		SetConvar("tas_strafe_pitch", cl.viewangles[PITCH], true);
	}
}

void Script_Playback_Host_Frame_Hook()
{
	// TODO: Make this function less disgusting
	
	if (tas_gamestate == loading)
		return;

	Savestate_Frame_Hook(playback.current_frame, playback.pause_frame);

	if (playback.In_Edit_Mode() && m_state != MouseState::Locked)
	{
		ApplyMouseStuff();
	}

	if (tas_gamestate == paused && playback.should_unpause)
	{
		tas_gamestate = unpaused;
		playback.should_unpause = false;
	}

	if (tas_gamestate == unpaused)
		m_state = MouseState::Locked;

	if (tas_gamestate == paused || !playback.script_running)
		return;
	else if (playback.current_frame >= playback.pause_frame 
	&& !svs.changelevel_issued && !scr_disabled_for_loading && (cls.state == ca_connected && cls.signon >= SIGNONS)) // make sure we dont TAS pause mid-load
	{
		tas_gamestate = paused;
		playback.script_running = false;
		VectorCopy(cl.viewangles, old_angles);
		return;
	}

	int current_block = playback.GetBlockNumber();

	while (current_block < playback.current_script.blocks.size()
	       && playback.current_script.blocks[current_block].frame <= playback.current_frame)
	{
		auto& block = playback.current_script.blocks[current_block];

		std::string cmd = block.GetCommand();
		playback.stacked.Stack(block);
		AddAfterframes(0, cmd.c_str(), NoFilter);
		++current_block;
	}

	playback.last_edited = Sys_DoubleTime();
	++playback.current_frame;
}

void Script_Playback_IN_Move_Hook(usercmd_t* cmd)
{
	if (tas_playing.value && tas_gamestate == paused)
	{
		if (m_state == MouseState::Locked || tas_freecam.value != 0)
		{
			VectorCopy(old_angles, cl.viewangles);
		}
		else if (m_state == MouseState::Pitch)
		{
			cl.viewangles[YAW] = old_angles[YAW];
		}
		else if (m_state == MouseState::Yaw)
		{
			cl.viewangles[PITCH] = old_angles[PITCH];
		}
	}
}

static int Get_Current_Frame()
{
	if (!tas_playing.value)
		return 0;

	return playback.current_frame;
}

PlaybackInfo& GetPlaybackInfo()
{
	return playback;
}

bool TAS_Script_Load(const char* name)
{
	Clear_Bookmarks();
	Savestate_Script_Updated(0);
	playback.current_script = TASScript(name);
	playback.last_edited = Sys_DoubleTime();
	bool result = playback.current_script.Load_From_File();

	return result;
}

static void CreateScriptName(char* buffer, const char* name)
{
	sprintf(buffer, "%s/tas/%s", com_gamedir, Cmd_Argv(1));
	COM_ForceExtension(buffer, ".qtas");
}

void Cmd_TAS_Script_Init(void)
{
	if (Cmd_Argc() < 4)
	{
		Con_Print("Usage: tas_script_init <filename> <map> <difficulty>\n");
		return;
	}

	char name[256];
	CreateScriptName(name, Cmd_Argv(1));

	playback.current_script = TASScript(name);
	FrameBlock b1;
	b1.frame = 1;
	b1.Add_Command("disconnect");
	b1.Add_Command("tas_set_seed");
	sprintf(BUFFER, "tas_strafe_version %s", tas_strafe_version.defaultvalue);
	b1.Add_Command(BUFFER);

	FrameBlock b2;
	b2.frame = 2;
	std::string map = Cmd_Argv(2);
	int difficulty = atoi(Cmd_Argv(3));
	b2.Add_Command("skill " + std::to_string(difficulty));
	b2.Add_Command("record demo " + map);

	FrameBlock b3;
	b3.frame = 8;

	playback.current_script.blocks.push_back(b1);
	playback.current_script.blocks.push_back(b2);
	playback.current_script.blocks.push_back(b3);
	playback.current_script.Write_To_File();
	Con_Printf("Initialized script into file %s, map %s and difficulty %d\n", name, map.c_str(), difficulty);
	Savestate_Script_Updated(0);
}

void Cmd_TAS_Script_Load(void)
{
	if (Cmd_Argc() == 1)
	{
		Con_Print("Usage: tas_script_load <filename>\n");
		return;
	}

	char name[256];
	sprintf(name, "%s/tas/%s", com_gamedir, Cmd_Argv(1));
	COM_ForceExtension(name, ".qtas");
	
	if (TAS_Script_Load(name))
	{
		tas_playing.value = 0;
		tas_gamestate = unpaused;
		Cbuf_AddText("disconnect");
	}
}

void Cmd_TAS_Script_Play(void)
{
	if (Cmd_Argc() > 1)
		Cmd_TAS_Script_Load();
	Run_Script(-1);
}

void Cmd_TAS_Script_Stop(void)
{
	playback.script_running = false;
	tas_playing.value = 0;
	ClearAfterframes();
	Cmd_TAS_Cmd_Reset();
}

void Cmd_TAS_Script_Skip(void)
{
	if (Cmd_Argc() == 1)
	{
		Con_Print("Usage: tas_script_skip <frame>\n");
		return;
	}

	int frame = atoi(Cmd_Argv(1));
	if (frame < 0)
		frame = playback.Get_Last_Frame() - frame - 1;
	Run_Script(frame, true);
}

void Skip_To_Block(int block)
{
	if (block >= playback.current_script.blocks.size() || block < LOWEST_BLOCK)
	{
		Con_Printf("Tried to move to block %d, which is out of bounds!\n", block);
		return;
	}

	int frame = playback.current_script.blocks[block].frame;
	Run_Script(frame, true);
}

void Cmd_TAS_Script_Skip_Block(void)
{
	if (Cmd_Argc() == 1)
	{
		Con_Print("Usage: tas_script_skip_block <block>\n");
		return;
	}

	int block = atoi(Cmd_Argv(1));
	if (block < 0)
	{
		int blocks_from_end = 0 - block;
		block = playback.current_script.blocks.size() - blocks_from_end;
	}

	if (block >= playback.current_script.blocks.size() || block < LOWEST_BLOCK)
	{
		Con_Printf("Tried to move to block %d, which is out of bounds!\n", block);
		return;
	}
	Skip_To_Block(block);
}

void Cmd_TAS_Script_Advance(void)
{
	if (playback.script_running)
	{
		Con_Printf("Can't advance while script still running.\n");
		return;
	}

	int frames;

	if (Cmd_Argc() > 1)
		frames = atoi(Cmd_Argv(1));
	else
		frames = 1;

	int target_frame = playback.current_frame + frames;
	Generic_Advance(target_frame);
}

void Cmd_TAS_Script_Advance_Block(void)
{
	if (playback.script_running)
	{
		Con_Printf("Can't advance while script still running.\n");
		return;
	}

	int blocks;

	if (Cmd_Argc() > 1)
		blocks = atoi(Cmd_Argv(1));
	else
		blocks = 1;

	int target_block = playback.GetBlockNumber() + blocks;
	auto curblock = playback.Get_Current_Block();

	if (curblock && curblock->frame != playback.current_frame && blocks > 0)
		target_block -= 1;

	if (target_block < LOWEST_BLOCK || target_block >= playback.current_script.blocks.size())
	{
		Con_Printf("Tried to move to block %d, which is out of bounds!\n", target_block);
		return;
	}

	auto& block = playback.current_script.blocks[target_block];
	int frame = block.frame;
	Generic_Advance(frame);
}

void Cmd_TAS_Edit_Prune(void)
{
	if (Cmd_Argc() == 3)
	{
		int min = std::atoi(Cmd_Argv(1));
		int max = std::atoi(Cmd_Argv(2));
		playback.current_script.Prune(min, max);
	}
	else
	{
		playback.current_script.Prune(LOWEST_FRAME);
	}
}

void Cmd_TAS_Edit_Save(void)
{
	if (Cmd_Argc() > 1)
	{
		CreateScriptName(BUFFER, Cmd_Argv(1));
		playback.current_script.file_name = BUFFER;
	}

	playback.current_script.Write_To_File();
}

float Get_Pitch_Convar(const char* name)
{
	if (Has_Existing_Value(name))
	{
		return Get_Existing_Value(name);
	}
	else
	{
		return cl.viewangles[PITCH];
	}
}

float Get_Yaw_Convar(const char* name)
{
	if (Has_Existing_Value(name))
	{
		return Get_Existing_Value(name);
	}
	else
	{
		return cl.viewangles[YAW];
	}
}

void Cmd_TAS_Edit_Strafe(void)
{
	current_strafe = Get_Existing_Value("tas_strafe");
	current_strafeyaw = Get_Existing_Value("tas_strafe_yaw");
	cl.viewangles[YAW] = Get_Yaw_Convar("tas_strafe_yaw");
	SetConvar("tas_strafe", 1, true);
	m_state = MouseState::Strafe;
}

void Cmd_TAS_Edit_Swim(void)
{
	current_strafe = Get_Existing_Value("tas_strafe");
	current_strafeyaw = Get_Existing_Value("tas_strafe_yaw");
	cl.viewangles[YAW] = Get_Yaw_Convar("tas_strafe_yaw");
	current_strafepitch = Get_Existing_Value("tas_strafe_pitch");
	cl.viewangles[PITCH] = Get_Yaw_Convar("tas_strafe_pitch");
	SetConvar("tas_strafe", 1, true);
	SetConvar("tas_strafe_type", 4, true);
	m_state = MouseState::Swim;
}

void Cmd_TAS_Edit_Set_Pitch(void)
{
	m_state = MouseState::Pitch;
	current_pitch = Get_Existing_Value("tas_view_pitch");
	cl.viewangles[PITCH] = Get_Pitch_Convar("tas_view_pitch");
}

void Cmd_TAS_Edit_Set_Yaw(void)
{
	m_state = MouseState::Yaw;
	current_yaw = Get_Existing_Value("tas_view_yaw");
	cl.viewangles[YAW] = Get_Yaw_Convar("tas_view_yaw");
}

void Cmd_TAS_Edit_Set_View(void)
{
	m_state = MouseState::Mixed;
	current_pitch = Get_Existing_Value("tas_view_pitch");
	cl.viewangles[PITCH] = Get_Pitch_Convar("tas_view_pitch");
	current_yaw = Get_Existing_Value("tas_view_yaw");
	cl.viewangles[YAW] = Get_Yaw_Convar("tas_view_yaw");
}

void Cmd_TAS_Edit_Shrink(void)
{
	int current_block = playback.GetBlockNumber();
	if (current_block >= playback.current_script.blocks.size())
	{
		Con_Printf("Already beyond last block.\n");
		return;
	}
	else
	{
		int new_size;

		if (CurrentFrameHasBlock())
			new_size = current_block + 1;
		else
			new_size = current_block;

		if (new_size < playback.current_script.blocks.size())
		{
			if (new_size <= LOWEST_BLOCK)
			{
				Con_Printf("Cannot delete all blocks!\n");
				return;
			}
			else
			{
				playback.current_script.blocks.resize(new_size);
				playback.last_edited = Sys_DoubleTime();
			}
		}
	}
}

void Cmd_TAS_Edit_Delete(void)
{
	int current_block = playback.GetBlockNumber();
	if (current_block <= LOWEST_BLOCK)
	{
		Con_Print("Cannot delete first block.\n");
		return;
	}
	else if (!CurrentFrameHasBlock())
	{
		Con_Print("Current frame has no block to delete.\n");
		return;
	}

	CenterPrint("Block deleted.\n");
	playback.current_script.blocks.erase(playback.current_script.blocks.begin() + current_block);
	playback.last_edited = Sys_DoubleTime();
}

void Shift_Block(const FrameBlock& new_block)
{
	playback.last_edited = Sys_DoubleTime();

	for (auto i = 0; i < playback.current_script.blocks.size(); ++i)
	{
		auto& block = playback.current_script.blocks[i];

		if (block.frame == new_block.frame)
		{
			Con_Printf("Cannot shift to an existing block! This is a bug.\n");
			return;
		}
		else if (block.frame > new_block.frame)
		{
			playback.current_script.blocks.insert(playback.current_script.blocks.begin() + i, new_block);
			return;
		}
	}

	playback.current_script.blocks.push_back(new_block);
}

void Cmd_TAS_Edit_Shift(void)
{
	if (!CurrentFrameHasBlock())
	{
		Con_Printf("No block found on current frame, nothing to shift!\n");
		return;
	}
	else if (Cmd_Argc() == 1)
	{
		Con_Printf("Usage: tas_edit_shift <frames>\n");
		return;
	}

	int frames = atoi(Cmd_Argv(1));
	int new_frame = playback.current_frame + frames;

	if (new_frame < LOWEST_FRAME)
	{
		Con_Printf("Cannot move frame past %d\n", LOWEST_FRAME);
		return;
	}
	else if (std::any_of(playback.current_script.blocks.begin(),
	                     playback.current_script.blocks.end(),
	                     [&](auto& element) { return element.frame == new_frame; }))
	{
		Con_Printf("Frame block already on frame %d\n", new_frame);
		return;
	}

	int current_block = playback.GetBlockNumber();
	auto block = playback.current_script.blocks[current_block];
	block.frame = new_frame;
	playback.current_script.blocks.erase(playback.current_script.blocks.begin() + current_block);
	Shift_Block(block);
	Generic_Advance(new_frame);
}

void Cmd_TAS_Edit_Shift_Stack(void)
{
	if (Cmd_Argc() == 1)
	{
		Con_Printf("Usage: tas_edit_shift <frames>\n");
		return;
	}

	int frames = atoi(Cmd_Argv(1));
	int current_block = playback.GetBlockNumber();

	if (playback.current_frame >= playback.Get_Last_Frame())
	{
		Con_Printf("Cannot shift stack, this is the last frame block.\n");
		return;
	}

	std::vector<FrameBlock> insert_blocks;

	// Frame check
	for (int i = playback.current_script.blocks.size() - 1;
	     i >= LOWEST_BLOCK && playback.current_script.blocks[i].frame > playback.current_frame;
	     --i)
	{
		auto& block = playback.current_script.blocks[i];
		int new_frame = block.frame + frames;

		if (new_frame < LOWEST_FRAME || new_frame <= playback.current_frame)
		{
			Con_Printf("Cannot move frame to position %d\n", new_frame);
			return;
		}
	}

	for (int i = playback.current_script.blocks.size() - 1;
	     i >= LOWEST_BLOCK && playback.current_script.blocks[i].frame > playback.current_frame;
	     --i)
	{
		auto block = playback.current_script.blocks[i];
		block.frame += frames;
		playback.current_script.blocks.pop_back();
		insert_blocks.push_back(block);
	}

	for (auto& block : insert_blocks)
		Shift_Block(block);
}

void Cmd_TAS_Edit_Add_Empty(void)
{
	GetBlockForFrame();
}

void Cmd_TAS_Edit_Shots(void)
{
	if (playback.current_script.blocks.empty())
	{
		Con_Print("Empty script\n");
		return;
	}

	if (Cmd_Argc() >= 3)
	{
		int min, max, count;

		if (Cmd_Argc() == 3)
		{
			count = std::atoi(Cmd_Argv(1));
			int frames = std::atoi(Cmd_Argv(2));
			int delay = GetPlayerWeaponDelay();
			min = playback.current_frame - frames;
			max = playback.current_frame - 1;
		}
		else
		{
			count = std::atoi(Cmd_Argv(1));
			min = std::atoi(Cmd_Argv(2));
			max = std::atoi(Cmd_Argv(3));
		}

		int delay = GetPlayerWeaponDelay();

		if (min < 0)
		{
			Con_Printf("Invalid lower bound %d\n", min);
			return;
		}
		else if (max < min)
		{
			Con_Printf("Invalid higher bound %d\n", max);
			return;
		}

		int* integers = GenerateRandomIntegers(count, min, max, delay);
		if (integers == NULL)
		{
			return;
		}

		playback.current_script.RemoveTogglesFromRange("attack", min, max);
		playback.current_script.Prune(min, max);

		for (int i = 0; i < count; ++i)
		{
			int frame = integers[i];
			SetToggle("attack", true, true, frame);
			SetToggle("attack", false, true, frame + 1);
		}
	}
	else
	{
		Con_Print("Usage: tas_edit_shots <count> <frames>\ntas_edit_shots <count> <min> <max>\n");
	}
}

void Cmd_TAS_Edit_Random_Toggle(void)
{
	if (playback.current_script.blocks.empty())
	{
		Con_Print("Empty script\n");
		return;
	}

	if (Cmd_Argc() >= 2)
	{
		auto toggle = Cmd_Argv(1);
		int min = std::atoi(Cmd_Argv(2));
		int max = std::atoi(Cmd_Argv(3));

		if (min < 0)
		{
			Con_Printf("Invalid lower bound %d\n", min);
			return;
		}
		else if (max < min)
		{
			Con_Printf("Invalid higher bound %d\n", max);
			return;
		}

		int* integers = GenerateRandomIntegers(1, min, max, 1);
		if (integers == NULL)
		{
			return;
		}

		playback.current_script.RemoveTogglesFromRange(toggle, min, max);
		playback.current_script.Prune(min, max);
		int frame = integers[0];
		SetToggle(toggle, true, true, frame);
	}
	else
	{
		Con_Print("Usage: tas_edit_random_toggle <command> <min> <max>\n");
	}
}

void Cmd_TAS_Confirm(void)
{
	if (m_state == MouseState::Locked)
		return;

	if (m_state == MouseState::Strafe)
		CenterPrint("Set strafe");
	else if (m_state == MouseState::Mixed)
		CenterPrint("Set view");
	else if (m_state == MouseState::Yaw)
		CenterPrint("Set yaw");
	else if (m_state == MouseState::Pitch)
		CenterPrint("Set pitch");
	else
		CenterPrint("Set swimming");
	m_state = MouseState::Locked;
}

void Cmd_TAS_Cancel(void)
{
	if (m_state == MouseState::Locked)
		return;

	if (m_state == MouseState::Strafe)
	{
		SetConvar("tas_strafe_yaw", current_strafeyaw, true);
		SetConvar("tas_strafe", current_strafe, true);
	}
	else if (m_state == MouseState::Swim)
	{
		SetConvar("tas_strafe_yaw", current_strafeyaw, true);
		SetConvar("tas_strafe_pitch", current_strafepitch, true);
		SetConvar("tas_strafe", current_strafe, true);
	}
	else
	{
		if (m_state == MouseState::Mixed || m_state == MouseState::Yaw)
		{
			SetConvar("tas_view_yaw", current_yaw, true);
		}

		if (m_state == MouseState::Mixed || m_state == MouseState::Pitch)
		{
			SetConvar("tas_view_pitch", current_pitch, true);
		}
	}

	m_state = MouseState::Locked;
}

void Cmd_TAS_Revert(void)
{
#define RETURN_STACKED(val) SetConvar(#val, Get_Stacked_Value(#val), true)

	if (m_state == MouseState::Locked)
		return;

	if (m_state == MouseState::Strafe)
	{
		RETURN_STACKED(tas_strafe_yaw);
		RETURN_STACKED(tas_strafe);
	}
	else if (m_state == MouseState::Swim)
	{
		RETURN_STACKED(tas_strafe_yaw);
		RETURN_STACKED(tas_strafe_pitch);
		RETURN_STACKED(tas_strafe);
	}
	else
	{
		if (m_state == MouseState::Mixed || m_state == MouseState::Yaw)
		{
			RETURN_STACKED(tas_view_yaw);
		}

		if (m_state == MouseState::Mixed || m_state == MouseState::Pitch)
		{
			RETURN_STACKED(tas_view_pitch);
		}
	}

	m_state = MouseState::Locked;
}


qboolean Script_Playback_Cmd_ExecuteString_Hook(const char* text)
{
	if (!playback.In_Edit_Mode())
		return qfalse;

	char* name = Cmd_Argv(0);
	static char lowercase [33];
	strcpy(lowercase, name);
	int len = strlen(name);
	for (int i = 0; i <= len; ++i) {
		lowercase[i] = tolower(name[i]);
	}

	if (tas_freecam.value != 0 && IsMovementCommand(lowercase))
	{
		SendMovementCommand(lowercase);
		return qtrue;
	}
	else if (IsGameplayCvar(lowercase))
	{
		if (Cmd_Argc() == 1)
		{
			Con_Printf("No argument for cvar given.\n");
			return qfalse;
		}

		float new_val = atof(Cmd_Argv(1));
		SetConvar(lowercase, new_val);

		return qtrue;
	}
	else if (IsDownCmd(lowercase))
	{
		auto cmd = lowercase + 1;

		bool old_value = Get_Existing_Toggle(cmd);
		bool new_value = !old_value;
		SetToggle(cmd, new_value);

		return qtrue;
	}
	else if (strstr(lowercase, "impulse") == lowercase && Cmd_Argc() > 1)
	{
		int number = atoi(Cmd_Argv(1));

		if (number < 1 || number > 9)
			return qfalse;

		playback.last_edited = Sys_DoubleTime();
		auto block = GetBlockForFrame();
		CenterPrint("Block: Added impulse %s", Cmd_Argv(1));
		block->commands.clear();
		snprintf(BUFFER, ARRAYSIZE(BUFFER), "impulse %s", Cmd_Argv(1));
		block->Add_Command(BUFFER);

		return qtrue;
	}

	return qfalse;
}

PlaybackInfo::PlaybackInfo()
{
	pause_frame = -1;
	current_frame = -1;
	script_running = false;
	should_unpause = false;
}

const FrameBlock* PlaybackInfo::Get_Current_Block(int frame) const
{
	int blck = GetBlockNumber(frame);

	if (blck >= playback.current_script.blocks.size())
		return nullptr;
	else
		return &playback.current_script.blocks[blck];
}

const FrameBlock* PlaybackInfo::Get_Stacked_Block() const
{
	return &stacked;
}

int PlaybackInfo::GetBlockNumber(int frame) const
{
	if (frame == -1)
		frame = current_frame;

	for (int i = 0; i < current_script.blocks.size(); ++i)
	{
		if (current_script.blocks[i].frame >= frame)
		{
			return i;
		}
	}

	return current_script.blocks.size();
}

int PlaybackInfo::Get_Number_Of_Blocks() const
{
	return current_script.blocks.size();
}

int PlaybackInfo::Get_Last_Frame() const
{
	if (playback.current_script.blocks.empty())
		return 0;
	else
	{
		return playback.current_script.blocks[playback.current_script.blocks.size() - 1].frame;
	}
}

bool PlaybackInfo::In_Edit_Mode() const
{
	return !script_running && tas_gamestate == paused && tas_playing.value && !current_script.blocks.empty();
}
