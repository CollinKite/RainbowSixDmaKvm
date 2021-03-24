#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <random>
#include <thread>
#include <chrono>
#include "../vmread/hlapi/hlapi.h"
#include "offsets.h"
#include "config.h"

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

static bool run_cheat = true;

uint64_t get_base(WinProcess &proc)
{
	PEB peb = proc.GetPeb();
	return peb.ImageBaseAddress;
}

void read_data(WinProcess &proc, R6Data &data, bool init = true)
{
	if (init)
	{
		data.base = get_base(proc);
	}

	auto localplayer = proc.Read<uint64_t>(data.base + PROFILE_MANAGER_OFFSET);
	localplayer = proc.Read<uint64_t>(localplayer + 0x68);
	localplayer = proc.Read<uint64_t>(localplayer + 0x0);
	localplayer = proc.Read<uint64_t>(localplayer + 0x28) + 0xebab0991057478ed;
	data.local_player = localplayer;

	auto fov_manager = proc.Read<uint64_t>(data.base + FOV_MANAGER_OFFSET);
	fov_manager = proc.Read<uint64_t>(fov_manager + 0xE8);
	fov_manager = proc.Read<uint64_t>(fov_manager + 0x88B932A0D99755B8);
	data.fov_manager = fov_manager;

	auto weapon = proc.Read<uint64_t>(localplayer + 0x90);
	weapon = proc.Read<uint64_t>(weapon + 0xc8);
	data.curr_weapon = weapon;

	auto weapon2 = proc.Read<uint64_t>(data.curr_weapon + 0x290) - 0x2b306cb952f73b96;
	data.weapon_info = weapon2;

	data.glow_manager = proc.Read<uint64_t>(data.base + GLOW_MANAGER_OFFSET);

	data.round_manager = proc.Read<uint64_t>(data.base + ROUND_MANAGER_OFFSET);

	data.game_manager = proc.Read<uint64_t>(data.base + GAME_MANAGER_OFFSET); 
}

void enable_esp(WinProcess &proc, const R6Data &data)
{
	if(!USE_CAV_ESP) return;
	bool updated = false;

	if(data.game_manager == 0) return;
	auto entity_list = proc.Read<uint64_t>(data.game_manager + 0x98) + 0xE60F6CF8784B5E96;
	if(entity_list == 0) return;
	for(int i = 0; i < 11; ++i)
	{
		auto entity_address = proc.Read<uint64_t>(entity_list + (0x8 * i)); //entity_object
		auto buffer = proc.Read<uint64_t>(entity_address + 0x18);
		auto size = proc.Read<uint32_t>(buffer + 0xE0) & 0x3FFFFFFF;
		auto list_address = proc.Read<uint64_t>(buffer + 0xD8);
		for(uint32_t i = 0; i < size; ++i)
		{
			auto pbuffer = proc.Read<uint64_t>(list_address + i * sizeof(uint64_t));
			auto current_vtable_rel = proc.Read<uint64_t>(pbuffer) - data.base;
			if(current_vtable_rel == VTMARKER_OFFSET)
			{
				auto spotted = proc.Read<uint8_t>(pbuffer + 0x632);
				if(!spotted)
				{
					updated = true;
					proc.Write<uint8_t>(pbuffer + 0x632, 1);
				}
			}
		}
	}
	if(updated)
	{
		printf("Cav esp updated\n");
	}
}

void enable_no_recoil(WinProcess &proc, const R6Data &data)
{
	if(!USE_NO_RECOIL) return;

	if(data.fov_manager == 0) return;
	proc.Write<float>(data.fov_manager + 0xE34, 0.f);
	proc.Write<float>(data.weapon_info + 0x198, 0.f); //rec b
	proc.Write<float>(data.weapon_info + 0x18c, 0.f); //rec v
	proc.Write<float>(data.weapon_info + 0x17c, 0.f); //rec h
	printf("No recoil active\n");
}

void enable_no_spread(WinProcess &proc, const R6Data &data)
{
	if(!USE_NO_SPREAD) return;

	if(data.weapon_info == 0) return;
	proc.Write<float>(data.weapon_info + 0x80, 0.f); // no spread
	printf("No spread active\n");
}

void enable_run_and_shoot(WinProcess &proc, const R6Data &data)
{
	if(!USE_RUN_AND_SHOOT) return;

	if(data.base == 0) return;
	proc.Write<uint8_t>(data.base + 0x1E59401 + 0x6, 0x1); // run and shoot
	proc.Write<uint8_t>(data.base + 0x33AE195 + 0x6, 0x1); // run and shoot
	printf("Run and shoot active\n");
}

void enable_no_flash(WinProcess &proc, const R6Data &data)
{
	if(!USE_NO_FLASH) return;

	if(data.local_player == 0) return;
	auto player = proc.Read<uint64_t>(data.local_player + 0x30);
	player = proc.Read<uint64_t>(player + 0x31);
	auto noflash = proc.Read<uint64_t>(player + 0x28);
	proc.Write<uint8_t>(noflash + 0x40, 0); // noflash
	printf("Noflash active\n");
}

void enable_no_aim_animation(WinProcess &proc, const R6Data &data)
{
	if(!USE_NO_AIM_ANIM) return;

	if(data.local_player == 0) return;
	auto no_anim = proc.Read<uint64_t>(data.local_player + 0x90);
	no_anim = proc.Read<uint64_t>(no_anim + 0xc8);
	proc.Write<uint8_t>(no_anim + 0x384, 0);
	printf("No aim animation active\n");
}

void set_fov(WinProcess &proc, const R6Data &data, float fov_val)
{
	if(!CHANGE_FOV) return;

	auto fov = proc.Read<uint64_t>(data.base + FOV_MANAGER_OFFSET);
	if(fov == 0) return;
	fov = proc.Read<uint64_t>(fov + 0x60) + 0xe658f449242c196;
	if(fov == 0) return;
	auto playerfov = proc.Read<uint64_t>(fov + 0x0) + 0x38;
	if(playerfov == 0) return;
	proc.Write<float>(playerfov, fov_val); // player fov ~1.2f-2.3f
	printf("Player fov changed to %f\n", fov_val);
}

void set_firing_mode(WinProcess &proc, const R6Data &data, FiringMode mode)
{
	if(!CHANGE_FIRING_MODE) return;

	if(data.curr_weapon == 0) return; 
	proc.Write<uint32_t>(data.curr_weapon + 0x118, (uint32_t)mode); //firing mode 0 - auto 3 - single 2 -  burst
	printf("Fire mode: %s\n", (mode == FiringMode::AUTO ? "auto" : mode == FiringMode::BURST ? "burst" : "single"));
}

void enable_glow(WinProcess &proc, const R6Data &data)
{
	if(!USE_GLOW) return;
	auto chain = proc.Read<uint64_t>(data.glow_manager + 0xb8);
	if (chain != 0)
	{
		proc.Write<float>(chain + 0xd0, 1.f); //r
		proc.Write<float>(chain + 0xd4, 1.f);	//g
		proc.Write<float>(chain + 0xd8, 0.f); //b
		proc.Write<float>(chain + 0x110, 100.f); //distance
		proc.Write<float>(chain + 0x118, 0.f); //a
		proc.Write<float>(chain + 0x11c, 1.f); //opacity
		printf("Glow active\n");
	}
	else
	{
		printf("[ERROR]: Can not activate glow\n");
	}
}

int32_t get_game_state(WinProcess &proc, const R6Data &data)
{
	if(data.round_manager == 0) return -1;
	return proc.Read<uint8_t>(data.round_manager + 0x2e8); // 3/2=in game 1=in operator selection menu 5=in main menu
}

//player is currently in operator selection menu
bool is_in_op_select_menu(WinProcess &proc, const R6Data& data)
{
	return get_game_state(proc, data) == 1;
}

bool is_in_main_menu(WinProcess &proc, const R6Data& data)
{
	return get_game_state(proc, data) == 5;
}

bool is_in_game(WinProcess &proc, const R6Data& data)
{
	return !is_in_op_select_menu(proc, data) && !is_in_main_menu(proc, data) && get_game_state(proc, data) != 0;
}

void unlock_all(WinProcess &proc, const R6Data& data)
{
	if(!UNLOCK_ALL) return;
	uint8_t shellcode[] = { 65, 198, 70, 81, 0, 144 };
	proc.Write(data.base + 0x271470B, shellcode, sizeof(shellcode));
	printf("Unlock all executed\n");
}

void update_all(WinProcess &proc, R6Data &data, ValuesUpdates& update)
{
	enable_esp(proc, data);
	if(update.update_no_recoil)
	{
		enable_no_recoil(proc, data);
		update.update_no_recoil = false;
		printf("No recoil updated\n");
	}
	if(update.update_no_spread)
	{
		enable_no_spread(proc, data);
		update.update_no_spread = false;
		printf("No spread updated\n");
	}	
	if(update.update_no_flash)
	{
		enable_no_flash(proc, data);
		update.update_no_flash = false;
		printf("No flash updated\n");
	}
	if(update.update_firing_mode)
	{
		set_firing_mode(proc, data, CURRENT_FIRE_MODE);
		update.update_firing_mode = false;
		printf("Firing mode updated\n");
	}
	if(update.update_fov)
	{
		set_fov(proc, data, NEW_FOV);
		update.update_fov = false;
		printf("Fov updated\n");
	}
	if(update.update_fast_aim)
	{
		enable_no_aim_animation(proc, data);
		update.update_fast_aim = false;
	}

	enable_glow(proc, data);
}

void check_update(WinProcess &proc, R6Data &data, ValuesUpdates& update)
{
	static bool esp_updated = false;

	if(USE_CAV_ESP)
	{
		if(esp_updated)
		{
			if(!is_in_game(proc, data))
			{
				esp_updated = false;
			}
		}
		if(!esp_updated && is_in_game(proc, data))
		{
			update.update_cav_esp = true;
			esp_updated = true;
		}
	}
	if(USE_NO_SPREAD)
	{
		float spread_val = proc.Read<float>(data.weapon_info + 0x80); 
		if(spread_val)
		{
			update.update_no_spread = true;
		}
	}
	if(USE_NO_RECOIL)
	{
		float no_recoil_b = proc.Read<float>(data.weapon_info + 0x198);
		if(no_recoil_b)
		{
			update.update_no_recoil = true;
		}		
	}
	if(USE_NO_FLASH)
	{
		auto player = proc.Read<uint64_t>(data.local_player + 0x30);
		player = proc.Read<uint64_t>(player + 0x31);
		auto noflash = proc.Read<uint64_t>(player + 0x28);
		auto noflash_value = proc.Read<uint8_t>(noflash + 0x40);
		if(noflash_value)
		{
			update.update_no_flash = true;
		}
	}	
	if(CHANGE_FIRING_MODE)
	{
		uint32_t firing_mode = proc.Read<uint32_t>(data.curr_weapon + 0x118);
		if(firing_mode != (uint32_t)CURRENT_FIRE_MODE)
		{
			update.update_firing_mode = true;
		}
	}
	if(CHANGE_FOV)
	{
		auto fov = proc.Read<uint64_t>(data.base + FOV_MANAGER_OFFSET);
		fov = proc.Read<uint64_t>(fov + 0x60) + 0xe658f449242c196;
		auto playerfov = proc.Read<uint64_t>(fov + 0x0) + 0x38;
		auto fov_value = proc.Read<float>(playerfov); 
		if(fov_value != NEW_FOV)
		{
			update.update_fov = true;
		}
	}
	if(USE_NO_AIM_ANIM)
	{
		auto no_anim = proc.Read<uint64_t>(data.local_player + 0x90);
		no_anim = proc.Read<uint64_t>(no_anim + 0xc8);
		auto val = proc.Read<uint8_t>(no_anim + 0x384);
		if(val)
		{
			update.update_fast_aim = true;
		}
	}
}

void write_loop(WinProcess &proc, R6Data &data)
{
	printf("Write thread started\n\n");
	ValuesUpdates update = {false,false,false,false,false,false,false,false};
	while (run_cheat)
	{
		read_data(proc, data, 
			data.base == 0
			|| data.fov_manager == 0
			|| data.curr_weapon == 0
			|| data.game_manager == 0 
			|| data.glow_manager == 0
			|| data.round_manager == 0
			|| data.weapon_info == 0);

		while((is_in_main_menu(proc, data)) 
				|| get_game_state(proc, data) == 0) //waiting until the game is started
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		check_update(proc, data, update);

		if(is_in_game(proc, data))
		{			
			update_all(proc, data, update);			
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	printf("Exiting...\n");
}

__attribute__((constructor)) static void init()
{
	pid_t pid = 0;
#if (LMODE() != MODE_EXTERNAL())
	pid = getpid();
#endif
	printf("Using Mode: %s\n", TOSTRING(LMODE));

	try
	{
		R6Data data;
		bool not_found = true;
		WinContext write_ctx(pid);
		WinContext check_ctx(pid);
		printf("Searching for a process...\n");
		while (not_found)
		{
			write_ctx.processList.Refresh();
			for (auto &i : write_ctx.processList)
			{
				if (!strcasecmp("RainbowSix.exe", i.proc.name))
				{
					short magic = i.Read<short>(i.GetPeb().ImageBaseAddress);
					auto peb = i.GetPeb();
					auto g_Base = peb.ImageBaseAddress;
					if (g_Base != 0)
					{
						printf("\nR6 found %lx:\t%s\n", i.proc.pid, i.proc.name);
						printf("\tBase:\t%lx\tMagic:\t%hx (valid: %hhx)\n", peb.ImageBaseAddress, magic, (char)(magic == IMAGE_DOS_SIGNATURE));
						printf("Press enter to start\n");
						getchar();
						read_data(i, data);

						printf("Base: 0x%lx\nFOV Manager: 0x%lx\nLocal player: 0x%lx\nWeapon: 0x%lx\nGlow manager: 0x%lx\nRound manager: 0x%lx\nGame manager: 0x%lx\n\n", 
								data.base, data.fov_manager, data.local_player, data.curr_weapon, data.glow_manager, data.round_manager, data.game_manager);

						if(get_game_state(i, data) == 5)
						{
							unlock_all(i, data);
						}
   					 	set_fov(i, data, NEW_FOV);

						std::thread write_thread(write_loop, std::ref(i), std::ref(data));
						write_thread.detach();

						not_found = false;
						break;
					}
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		while (run_cheat)
		{
			bool flag = false;
			check_ctx.processList.Refresh();
			for (auto &i : check_ctx.processList)
			{
				if (!strcasecmp("RainbowSix.exe", i.proc.name))
				{
					auto peb = i.GetPeb();
					auto g_Base = peb.ImageBaseAddress;
					if (g_Base != 0)
					{
						flag = true;
						break;
					}
				}
			}
			if (!flag)
				run_cheat = false;

			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}
	catch (VMException &e)
	{
		printf("Initialization error: %d\n", e.value);
	}
}

int main()
{
	return 0;
}
