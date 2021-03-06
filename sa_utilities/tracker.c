/*
 * 07/11
 *
 * Track objects through a movie for multi fit STORM analysis.
 * Works "in place".
 *
 *
 * Hazen
 * 
 * Compilation instructions:
 *
 * Linux:
 *  gcc tracker.c -o tracker -lm
 *
 * Windows:
 *  gcc tracker.c -o tracker
 */


/* Include */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "insight.h"


/* Define */

#define USEAVERAGE 1 /* Use average position of objects in track as track center. */


/* Structures */

struct track_elt
{
  float object_data[OBJECT_DATA_SIZE];
  float x_center;
  float y_center;
  float x_center_total;
  float y_center_total;
  float total_weight;
  int molecule_id;
  int last_frame;
  int track_length;
  struct track_elt *next_object;
  struct track_elt *last_object;
  struct track_elt *next_track;
};


/* Globals */

struct track_elt *current_tracks;


/*
 * Tracks are organized like this:
 *
 * T10 - T20 - T30 - T40 -...
 *  |     |           |
 * T11   T21         T41
 *  |                 |
 * T12               T42
 *  |                 |
 * ...
 */


/* Functions */


/*
 * Create a track object with object_data.
 *
 * Input:
 *   float *object_data - A pointer to the molecule data in Insight3 format,
 *                        This is copied so the original data can be freed.
 *   int molecule_id - which number molecule this is.
 *
 * Returns:
 *   A pointer to the newly created track object.
 */

struct track_elt* createTrackObject(float *object_data, int molecule_id)
{
  int i;
  struct track_elt *new_track;

  new_track = (struct track_elt *) malloc (sizeof(struct track_elt));
  new_track->next_object = NULL;
  new_track->last_object = NULL;
  new_track->next_track  = NULL;
  new_track->molecule_id = molecule_id;
  for(i=0;i<(OBJECT_DATA_SIZE);i++){
    new_track->object_data[i] = object_data[i];
  }
  return new_track;
}


/*
 * Add a object to the current list of tracks.
 *
 * If the object is not near enough to a pre-existing track
 * it becomes a new track.
 *
 * The center is the weighted average of the x,y in the track
 *
 * Input:
 *   float *object_data - A pointer to the molecule data in Insight3 format,
 *                        This is copied so the original data can be freed.
 *   r_sqr_max - The square of the cutoff radius.
 *   molecule_id - which number molecule this is.
 *   frame_no - internal index for dealing w/ activation frames, etc.
 *
 * Returns nothing.
 */

void addObject(float *object_data, float r_sqr_max, int molecule_id, int frame_no)
{
  int i, found = 0, *object_data_int;
  float dx, dy, weight;
  struct track_elt *cur, *last, *new_track, *new_object;

  /* These are for reading out bits of object_data as integers (32 bit).*/
  object_data_int = (int *)object_data;

  /* 
   * Walk the current list. The object is added to *ALL* the tracks
   * that it is close enough too.
   */
  last = cur = current_tracks;
  while(cur != NULL){

    dx = cur->x_center - object_data[XO];
    dy = cur->y_center - object_data[YO];
    if((dx*dx+dy*dy)<r_sqr_max){

      /* Create track object */
      new_object = createTrackObject(object_data, molecule_id);

      /* Add to track */
      if(cur->last_object != NULL){
	cur->last_object->next_object = new_object;
      }
      else{
	cur->next_object = new_object;
      }
      cur->last_object = new_object;
      cur->track_length++;

      /* Update track data */
      cur->last_frame = frame_no;
      weight = sqrt(object_data[HEIGHT]);
      cur->x_center_total += weight * object_data[XO];
      cur->y_center_total += weight * object_data[YO];
      cur->total_weight += weight;

      if (USEAVERAGE){
	/*
	 * Use the weighted average of all the localizations as 
	 * the track center.
	 */
	cur->x_center = cur->x_center_total/cur->total_weight;
	cur->y_center = cur->y_center_total/cur->total_weight;
      }
      else{
	/*
	 * Use the location of the most recently added
	 * localization as the track center.
	 */
	cur->x_center = object_data[XO];
	cur->y_center = object_data[YO];
      }

      found = 1;
    }

    last = cur;
    cur = cur->next_track;
  }

  /*
   * If a object was found not we create a new track & add it
   * to the end of our list.
   */
  if(found==0){
    new_track = createTrackObject(object_data, molecule_id);
    weight = sqrt(object_data[HEIGHT]);
    new_track->x_center_total = weight * object_data[XO];
    new_track->y_center_total = weight * object_data[YO];
    new_track->total_weight = weight;
    new_track->x_center = object_data[XO];
    new_track->y_center = object_data[YO];
    new_track->last_frame = frame_no;
    new_track->track_length = 1;
    if (current_tracks == NULL){
      current_tracks = new_track;
    }
    else{
      last->next_track = new_track;
    }
  }
}


/*
 * Frees the data in a track.
 *
 * Input:
 *   struct track_elt *start - Pointer to the first structure in the track.
 *
 * Returns nothing.
 */

void freeTrack(struct track_elt *start)
{
  struct track_elt *current, *next_object;

  current = start;
  while(current != NULL){
    next_object = current->next_object;
    free(current);
    current = next_object;
  }
}


/*
 * Removes all the tracks from the list that have not seen in object since
 * frame number cull_frame.
 *
 * Input:
 *   FILE *mlist - File pointer to an open Insight3 format file.
 *   int cull_frame - The frame before which tracks are considered terminated.
 *
 * Returns:
 *   Number of tracks that were culled.
 */

int cullTracks(FILE *mlist, int cull_frame)
{
  int i, culled, *object_data_int, first_cat;
  float *object_data;
  struct track_elt *cur, *last, *to_save;

  culled = 0;

  last = cur = current_tracks;
  while(cur != NULL){
    if (cur->last_frame < cull_frame){

      /* 
       * Remove from the current list.
       */
      if(cur == current_tracks){ /* remove from the head of the list */
	current_tracks = cur->next_track;
	last = cur;
      }
      else if(cur->next_track == NULL){ /* remove from the tail of the list */
	last->next_track = NULL;
	last = cur;
      }
      else { /* remove from the middle of the list */
	last->next_track = cur->next_track;
      }

      /*
       * Edit molecule data for molecules in the track & save.
       */
      to_save = cur;
      object_data_int = (int *)to_save->object_data;
      first_cat = object_data_int[CAT];
      while(to_save != NULL){
	object_data = to_save->object_data;
	object_data_int = (int *)to_save->object_data;
	object_data_int[CAT] = first_cat;
	object_data_int[TLEN] = cur->track_length;
	object_data_int[VISITED] = 0;
	if(to_save->next_object != NULL){
	  object_data_int[LINK] = to_save->next_object->molecule_id;
	}
	else{
	  object_data_int[LINK] = 0;
	}

	fseeko64(mlist, DATA + OBJECT_DATA_SIZE*DATUM_SIZE*(long long)to_save->molecule_id, SEEK_SET);
	fwrite(object_data, sizeof(float), OBJECT_DATA_SIZE, mlist);
	to_save = to_save->next_object;
      }
     
      /* update pointers & free saved track */
      to_save = cur;
      cur = cur->next_track;
      freeTrack(to_save);

      culled++;
    }
    else {
      last = cur; 
      cur = cur->next_track;
    }
  }

  return culled;
}


/*
 * Main
 *
 * Descriptor is a string of the form "02110311" that 
 * describes the different frames.
 *   0 - activation frame
 *   1 - non-specific frame (category 0)
 *   2 - specific frame (category 1)
 *   3 - specific frame (category 2)
 *   ...
 *
 */

int main(int argc, const char *argv[])
{
  char version[5],tmp[2];
  int i, cur_frame, track_number, frame_diff, last_frame;
  int molecules, temp, desc_len, frame_no, cur_desc;
  int *object_data_int, *descriptor;
  float max_radius, zmin, zmax, object_data[OBJECT_DATA_SIZE];
  FILE *mlist;

  if (argc != 6){
    printf("usage tracker <mlist file> <descriptor> <radius> <zmin> <zmax>\n");
    exit(0);
  }

  /* 
   * Setup 
   */

  object_data_int = (int *)object_data;

  mlist = fopen(argv[1], "rb+");
  if (!mlist){
    printf("tracker: Could not open localization file %s\n", argv[1]);
    exit(0);
  }

  fseek(mlist, MOLECULES, SEEK_SET);
  fread(&molecules, sizeof(int), 1, mlist);
  printf("Molecules: %d (%s)\n", molecules, argv[1]);

  tmp[1] = (char)0;
  desc_len = strlen(argv[2]);
  descriptor = (int *)malloc(sizeof(int)*desc_len);
  for(i=0;i<desc_len;i++){
    tmp[0] = argv[2][i];
    descriptor[i] = atoi(tmp)-1;
  }

  max_radius = atof(argv[3]);
  max_radius = max_radius * max_radius;
  zmin = atof(argv[4]);
  zmax = atof(argv[5]);

  /*
   * Go through all the molecules & generate tracks.
   *
   * Ignore molecules found in activation frames (if any).
   */
  cur_frame = last_frame = frame_no = 0;
  track_number = 0;
  for(i=0;i<molecules;i++){
    if((i%50000)==0){
      printf("Processing molecule %d in frame %d (tracker)\n", i, cur_frame);
      //printf(" (%f, %f, %f)\n", object_data[X], object_data[Y], object_data[Z]);
    }
    fseeko64(mlist, DATA + OBJECT_DATA_SIZE*DATUM_SIZE*(long long)i, SEEK_SET);
    fread(&object_data, sizeof(float), OBJECT_DATA_SIZE, mlist);
    cur_frame = object_data_int[FRAME];
    cur_desc = descriptor[(cur_frame-1)%desc_len];

    if (cur_frame != last_frame){
      last_frame = cur_frame;
      if (cur_desc >= 0){
	track_number += cullTracks(mlist, frame_no);
	frame_no++;
      }
    }

    // mark out of z range peaks as category 9
    if((object_data[ZO] <= zmin)||(object_data[ZO] >= zmax)){
      cur_desc = 9;
    }

    object_data_int[CAT] = cur_desc;
    // not an activation frame or category 9
    if((cur_desc >= 0)&&(cur_desc != 9)){    
      addObject(object_data, max_radius, i, frame_no);
    }
    else{
      object_data_int[VISITED] = 0;
      object_data_int[LINK] = 0;
      fseeko64(mlist, DATA + OBJECT_DATA_SIZE*DATUM_SIZE*(long long)i, SEEK_SET);
      fwrite(object_data, sizeof(float), OBJECT_DATA_SIZE, mlist);
    }
  }
  printf("Finished processing\n");

  /* Update the remaining tracks */
  track_number += cullTracks(mlist, frame_no + 10);
  printf("Found %d tracks\n", track_number);

  fclose(mlist);
}


/*
 * The MIT License
 *
 * Copyright (c) 2012 Zhuang Lab, Harvard University
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
