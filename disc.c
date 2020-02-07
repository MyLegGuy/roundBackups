/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License version 2 or later
  as published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
// libburn is intended for Linux systems with kernel 2.4 or 2.6 for now
#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "config.h"
#include "disc.h"
/////////////////////////////////
static int getlba(struct burn_toc_entry* entry){
	if (entry->extensions_valid & 1) {
		/* DVD extension valid */
		return entry->start_lba;
	} else {
		return burn_msf_to_lba(entry->pmin, entry->psec, entry->pframe);
	}
}
//static char trackIsAudio(struct burn_toc_entry* entry){
//	return ((entry->control & 7) < 4);
//}
signed char getDataTrackPositions(struct burn_drive* drive, int** _retArray, int* _retLen){
	*_retArray=NULL;
	*_retLen=-1;
	struct burn_disc* disc=burn_drive_get_disc(drive);
	if (!disc){
		fprintf(stderr,"burn_drive_get_disc failed\n");
		return -2;
	}
	struct burn_session** sessions = burn_disc_get_sessions(disc, _retLen); // one track per session so this is ok
	*_retArray = malloc(sizeof(int)*(*_retLen)*2);	
	int i;
	for (i=0;i<*_retLen;++i){
		int numTracks;
		struct burn_track** tracks = burn_session_get_tracks(sessions[i],&numTracks);
		if (numTracks!=1){
			fprintf(stderr,"wrong number of tracks (%d) in session %d. expected only 1\n",numTracks,i);
			(*_retArray)[i*2]=-1;
			continue;
		}
		struct burn_toc_entry _curEntry;
		// get the start
		burn_track_get_entry(tracks[0],&_curEntry);
		(*_retArray)[i*2] = getlba(&_curEntry);
		// get the end
		burn_session_get_leadout_entry(sessions[i],&_curEntry);
		(*_retArray)[i*2+1] = getlba(&_curEntry);
		if ((*_retArray)[i*2+1]<=(*_retArray)[i*2]){ // if end <= start
			(*_retArray)[i*2]=-1;
			fprintf(stderr,"error - bad leadout lba %d;%d\n",(*_retArray)[i*2],(*_retArray)[i*2+1]);
			continue;
		}
	}
	burn_disc_free(disc);
	return 0;
}
signed char chunkToFile(struct burn_drive* drive, int _startSector, int _endSector, const char* _out){
	FILE* fp = fopen(_out,"wb");
	if (!fp){
		return -2;
	}
	signed char _ret=0;
	int sectorSize = 2048;
	int chunkSize = 16;
	char buf[chunkSize * 2048];
	for (;_startSector!=_endSector;){
		if (!(_endSector-chunkSize>=_startSector)){
			chunkSize=_endSector-_startSector;
		}
		off_t _numRead;
		if (burn_read_data(drive,(off_t)_startSector*sectorSize,buf,(off_t)(chunkSize * sectorSize),&_numRead, 1)<=0){
			fprintf(stderr,"Read error\n");
			_ret=-2;
			break;
		}
		// TODO - i think i read somewhere that it will always read as many as you tell it to except for the last call if the disc is out of space
		// 	actually, i was looking at burn_source read function.
		_startSector+=chunkSize;
		if (_numRead > 0) {
			if (fwrite(buf,1,_numRead,fp)!=_numRead){
				perror("Write error\n");
				_ret=-2;
				break;
			}
		}
	}
	if (fclose(fp)==EOF){
		perror("chunkToFile");
		_ret=-2;
	}
	return _ret;
}
/////////////////////////////////
/** Formats unformatted DVD-RW to profile 0013h "Restricted Overwrite"
    which needs no blanking for re-use but is not capable of multi-session.
    Expect a behavior similar to blanking with unusual noises from the drive.

    Formats unformatted BD-RE to default size. This will allocate some
    reserve space, test for bad blocks and make the media ready for writing.
    Expect a very long run time.

    Formats unformatted blank BD-R to hold a default amount of spare blocks
    for eventual mishaps during writing. If BD-R get written without being
    formatted, then they get no such reserve and will burn at full speed.

	returns -1 if not a BD
	returns 0 on fail. 1 on OK. 1 is returned also if the disc is already formatted
*/
int libburner_formatBD(struct burn_drive *drive){
	int current_profile;
	char current_profile_name[80];
	if (!burn_disc_get_profile(drive, &current_profile, current_profile_name)){
		fprintf(stderr,"No profile info avalible\n");
		return 0;
	}
	int ret,format_flag= 0;
	off_t size = 0;
	if (current_profile == 0x13) {
		//fprintf(stderr, "IDLE: DVD-RW media is already formatted\n");
		return 1;
	} else if (current_profile == 0x41 || current_profile == 0x43) {
		enum burn_disc_status disc_state = burn_disc_get_status(drive);
		if (disc_state != BURN_DISC_BLANK && current_profile == 0x41) {
			//fprintf(stderr,"FATAL: BD-R is not blank. Cannot format.\n");
			return 1;
		}
		unsigned dummy;
		int status;
		int num_formats;
		ret = burn_disc_get_formats(drive, &status, &size, &dummy, &num_formats);
		if (ret > 0 && status != BURN_FORMAT_IS_UNFORMATTED) {
			//fprintf(stderr,"IDLE: BD media is already formatted\n");
			return 1;
		}
		size = 0;           /* does not really matter */
		format_flag = 3<<1; /* format to default size, no quick */
	} else {
		//fprintf(stderr, "FATAL: Can only format BD\n");
		// can only format BD!
		return -1;
	}
	burn_set_signal_handling("libburner : ", NULL, 0x30);

	printf("Beginning to format media.\n");
	burn_disc_format(drive, size, format_flag);

	// Display progress
	struct burn_progress p;
	double percent = 1.0;
	while (burn_drive_get_status(drive, &p) != BURN_DRIVE_IDLE) {
		if(p.sectors>0 && p.sector>=0) /* display 1 to 99 percent */
			percent = 1.0 + ((double) p.sector+1.0)
					 / ((double) p.sectors) * 98.0;
		printf("Formatting  ( %.1f%% done )\n", percent);
		sleep(PROGRESSUPDATETIME);
	}
	if (burn_is_aborting(0) > 0)
		return 0;
	burn_set_signal_handling("libburner : ", NULL, 0x0);
	burn_disc_get_profile(drive, &current_profile,current_profile_name);
	if (current_profile == 0x14 || current_profile == 0x13)
		printf("Media type now: %4.4xh  \"%s\"\n",current_profile, current_profile_name);
	if (current_profile == 0x14) {
		fprintf(stderr,
		  "FATAL: Failed to change media profile to desired value\n");
		return 0;
	}
	return 1;
}
// returns -1 on error
// returns 0 if not error
static signed char initTrack(struct burn_drive* drive, struct burn_session* session, struct burn_track** _destTrack, struct burn_source** _destSource, int* fileDesDest){
	signed char _ret=0;
	//if (all_tracks_type != BURN_AUDIO) {
	// a padding of 300 kiB helps to avoid the read-ahead bug
	int padding = 300*1024;
	int fifo_chunksize = 2048;
	int fifo_chunks = 2048; /* 4 MB fifo */
	
	// init track
	*_destTrack = burn_track_create();
	burn_track_define_data(*_destTrack, 0, padding, 1, BURN_MODE1);
	
	// make file descriptors
	if (pipe(fileDesDest)==-1){
		perror("initTrack pipe failed");
		return -1;
	}
	// Convert this filedescriptor into a burn_source object
	struct burn_source* data_src = burn_fd_source_new(fileDesDest[0], -1, 0);
	if (data_src == NULL) {
		fprintf(stderr,"FATAL: Could not open data source\n");
		if(errno!=0){
			fprintf(stderr,"(Most recent system error: %s )\n", strerror(errno));
		}
		return -1;
	}
	// Install a fifo object on top of that data source object
	*_destSource = burn_fifo_source_new(data_src,fifo_chunksize, fifo_chunks, 0);
	if (!(*_destSource)) {
		fprintf(stderr,
				"FATAL: Could not create fifo object of 4 MB\n");
		{_ret=-1; goto ex;}
	}

	// Use the fifo object as data source for the track
	if (burn_track_set_source(*_destTrack, *_destSource) != BURN_SOURCE_OK) {
		fprintf(stderr, "FATAL: Cannot attach source object to track object\n");
		{_ret=-1; goto ex;}
	}
	// shove it in
	burn_session_add_track(session, *_destTrack, BURN_POS_END);
	// free
ex:
	burn_source_free(data_src);
	data_src = NULL;
	return _ret;
}
// returns -2 on fail
static signed char waitBurnComplete(struct burnState* _state){
	time_t start_time = time(0);
	int last_sector = 0;
	// wait for the burn process to spawn
	while (burn_drive_get_status(_state->drive, NULL) == BURN_DRIVE_SPAWNING)
		usleep(100002);
	//
	struct burn_progress progress;
	while (burn_drive_get_status(_state->drive, &progress) != BURN_DRIVE_IDLE) {
		if (progress.sectors <= 0 ||
		    (progress.sector == last_sector)){
			printf("Thank you for being patient for %d seconds.", (int) (time(0) - start_time));
		}else{
			printf("Track %d : sector %d", progress.track+1,progress.sector);
		}
		last_sector = progress.sector;
		if (progress.track >= 0 && progress.track < 1) {
			int size, free_bytes, ret;
			char *status_text;
	
			ret = burn_fifo_inquire_status(_state->fifo_source, &size, &free_bytes, &status_text);
			if (ret >= 0 ) 
				printf("  [fifo %s, %2d%% fill]", status_text,
					   (int) (100.0 - 100.0 *
							  ((double) free_bytes) /
							  (double) size));
		} 
		printf("\n");
		sleep(PROGRESSUPDATETIME);
	}
	printf("\n");
	return 0;
}
void zeroBurnState(struct burnState* _info){
	_info->target_disc=NULL;
	_info->session=NULL;
	_info->myTrack=NULL;
	_info->fifo_source=NULL;
	_info->options=NULL;
}
static void lowFreeBurnState(struct burnState* _info){
	if (_info->options)
		burn_write_opts_free(_info->options);
	if (_info->fifo_source)
		burn_source_free(_info->fifo_source);
	if (_info->myTrack)
		burn_track_free(_info->myTrack);
	if (_info->session)
		burn_session_free(_info->session);
	if (_info->target_disc)
		burn_disc_free(_info->target_disc);
}
// returns 1 if disc is in drive and isn't full and is appendable
static char discIsGood(struct burn_drive* drive){
	enum burn_disc_status disc_state = burn_disc_get_status(drive);
	if (disc_state != BURN_DISC_BLANK && disc_state != BURN_DISC_APPENDABLE) {
		if (disc_state == BURN_DISC_FULL) {
			fprintf(stderr, "FATAL: Detected closed media with data. Need blank or appendable media.\n");
			if (burn_disc_erasable(drive))
				fprintf(stderr, "HINT: Try blanking it first\n");
		} else if (disc_state == BURN_DISC_EMPTY) {
			fprintf(stderr,"FATAL: No media detected in drive\n");
		}else{
			fprintf(stderr,"FATAL: Cannot recognize state of drive and media\n");
		}
		return 0;
	}
	return 1;
}
/** Brings preformatted track images (ISO 9660, audio, ...) onto media.
    To make sure a data image is fully readable on any Linux machine, this
    function adds 300 kiB of padding to the (usualy single) track.
    Audio tracks get padded to complete their last sector.
    A fifo of 4 MB is installed between each track and its data source.
    Each of the 4 MB buffers gets allocated automatically as soon as a track
    begins to be processed and it gets freed as soon as the track is done.
    The fifos do not wait for buffer fill but writing starts immediately.

    In case of external signals expect abort handling of an ongoing burn to
    last up to a minute. Wait the normal burning timespan before any kill -9.
*/
int discStartWrite(struct burn_drive *drive, int multi, struct burnState* _state){
	zeroBurnState(_state);
	_state->drive=drive;
	//
	_state->target_disc = burn_disc_create();
	_state->session = burn_session_create();
	burn_disc_add_session(_state->target_disc, _state->session, BURN_POS_END);
	// init tracks
	if (initTrack(drive,_state->session,&(_state->myTrack),&(_state->fifo_source),_state->io)){
		fprintf(stderr,"initTrack failed\n");
		goto err;
	}
	// Evaluate drive and media
	// You must do this before
	if (!discIsGood(drive)){
		goto err;
	}
	// set up the options	
	_state->options = burn_write_opts_new(drive);
	//burn_write_opts_set_underrun_proof(burn_options, 1); // This is only needed with CD media and possibly with old DVD-R drives.
	burn_write_opts_set_perform_opc(_state->options, 0);
	burn_write_opts_set_multi(_state->options, !!multi);
	burn_drive_set_speed(drive, 0, 0);
	// magic function to call before.
	// "this function tries to find a suitable write type and block type for a given write job"
	{char reasons[BURN_REASONS_LEN];		
		if (burn_write_opts_auto_write_type(_state->options, _state->target_disc,reasons, 0) == BURN_WRITE_NONE) {
			fprintf(stderr, "FATAL: Failed to find a suitable write mode with this media.\n");
			fprintf(stderr, "Reasons given:\n%s\n", reasons);
			goto err;
		}
	}
	burn_set_signal_handling("libburner : ", NULL, 0x30);
	burn_disc_write(_state->options, _state->target_disc);
	return 0;
err:
	lowFreeBurnState(_state);
	free(_state);
	return 1;
}
// this assumes that you already closed the write pipe
// returns nonzero on error
signed char completeAndFreeBurn(struct burnState* _info){
	signed char ret=0;
	if (waitBurnComplete(_info)){
		fprintf(stderr,"waitBurnComplete failed in completeAndFreeBurn\n");
		ret=1;
	}
	if (close(_info->io[0])){
		perror("close read");
		ret=1;
	}
	if (burn_is_aborting(0) > 0){
		ret=1;
	}
	if (!burn_drive_wrote_well(_info->drive)){
		ret=1;
	}
	//if (multi && current_profile != 0x1a && current_profile != 0x13 && current_profile != 0x12 && current_profile != 0x43) {
	//	/* not with DVD+RW, formatted DVD-RW, DVD-RAM, BD-RE */
	//	printf("NOTE: Media left appendable.\n");
	//}
	lowFreeBurnState(_info);
	return ret;
}
int getWriteDescriptor(struct burnState* _info){
	return _info->io[1];
}
/////////////////////////////////
size_t getDiscFreeSpace(struct burn_drive* d){
	return burn_disc_available_space(d, NULL);
}
/*  If the persistent drive address is known, then this approach is much
    more un-obtrusive to the systemwide livestock of drives. Only the
    given drive device will be opened during this procedure.
    Special drive addresses stdio:<path> direct output to a hard disk file
    which will behave much like a DVD-RAM.
*/
static int getDriveByAddr(char *drive_adr, struct burn_drive_info** _retList){
	int ret;
	char libburn_drive_adr[BURN_DRIVE_ADR_LEN];
	/* This tries to resolve links or alternative device files */
	if ((ret = burn_drive_convert_fs_adr(drive_adr, libburn_drive_adr))<=0) {
		fprintf(stderr, "Address does not lead to a CD burner: '%s'\n",drive_adr);
		return 0;
	}
	fprintf(stderr,"Acquiring drive '%s' ...\n", libburn_drive_adr);
	if ((ret = burn_drive_scan_and_grab(_retList, libburn_drive_adr, 1)) <= 0) {
		fprintf(stderr,"FAILURE with persistent drive address  '%s'\n",libburn_drive_adr);
	}
	return ret;
}
// returns nonzero on fail
char initDiscLib(){
	if (!burn_initialize()){
		fprintf(stderr,"FATAL: Failed to initialize libburn.\n");
		return 1;
	}
	// Print messages of severity WARNING or more directly to stderr
	burn_msgs_set_severities("NEVER", "WARNING", "roundBackupsDisc : ");
	// Activate the default signal handler
	burn_set_signal_handling("roundBackupsDisc : ", NULL, 0);
	return 0;
}
// returns NULL on fail
struct burn_drive_info* openDrive(char* _path){
	struct burn_drive_info* _ret;
	if (getDriveByAddr(_path,&_ret)<=0) {
		fprintf(stderr,"FATAL: Failed to aquire drive.\n");
		return NULL;
	}
	return _ret;
}
struct burn_drive* getDrive(struct burn_drive_info* _info){
	return _info[0].drive;
}
void freeDrive(struct burn_drive_info* _info){
	burn_drive_release(_info[0].drive, 0);
	burn_drive_info_free(_info);
}
void deinitDiscLib(){
	burn_finish();
}
