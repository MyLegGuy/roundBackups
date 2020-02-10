/*
	Copyright (C) 2020  MyLegGuy
	This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
	This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
	You should have received a copy of the GNU General Public License along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include <libburn/libburn.h>
struct burnState{
	struct burn_disc* target_disc;
	struct burn_session* session;
	struct burn_track* myTrack;
	struct burn_source* fifo_source;
	struct burn_write_opts* options;
	struct burn_drive* drive; // not freed, but is here to allow this to be the only arg to the functions
	int io[2];
};
signed char getDataTrackPositions(struct burn_drive* drive, int** _retArray, int* _retLen);
int discStartWrite(struct burn_drive *drive, int multi, struct burnState* _state);
signed char completeAndFreeBurn(struct burnState* _info);
int getWriteDescriptor(struct burnState* _info);
char initDiscLib();
struct burn_drive_info* openDrive(char* _path);
struct burn_drive* getDrive(struct burn_drive_info* _info);
void freeDrive(struct burn_drive_info* _info);
void deinitDiscLib();
size_t getDiscFreeSpace(struct burn_drive* d);
int libburner_formatBD(struct burn_drive *drive);
