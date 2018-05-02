/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <errno.h>

#include "../gameshared/q_math.h"
#include "../qcommon/qcommon.h"
#include "mm_rating.h"

mm_uuid_t *Uuid_FromString( const char *buffer, mm_uuid_t *dest ) {
	unsigned long long groups[5];
	int expectedHyphenIndices[4] = { 8, 13, 18, 23 };
	const char *currptr;
	char *endptr = "";
	int i;

	if( !buffer ) {
		return NULL;
	}

	currptr = buffer;
	for( i = 0; i < 5; ++i ) {
		groups[i] = strtoull( currptr, &endptr, 16 );
		if( groups[i] == ULLONG_MAX && errno == ERANGE ) {
			return NULL;
		}
		if( *endptr != '-' ) {
			if( i != 4 && *endptr != '\0' ) {
				return NULL;
			}
		} else if( endptr - buffer != expectedHyphenIndices[i] ) {
			return NULL;
		}
		currptr = endptr + 1;
	}

	// If there are any trailing characters
	if( *endptr != '\0' ) {
		return NULL;
	}

	dest->hiPart = ( ( ( groups[0] << 16 ) | groups[1] ) << 16 ) | groups[2];
	dest->loPart = ( groups[3] << 48 ) | groups[4];
	return dest;
}

char *Uuid_ToString( char *buffer, const mm_uuid_t uuid ) {
	const char *format = "%08" PRIx64 "-%04" PRIx64 "-%04" PRIx64 "-%04" PRIx64 "-%012" PRIx64;
	uint64_t groups[5];
	groups[0] = ( uuid.hiPart >> 32 ) & 0xFFFFFFFFul;
	groups[1] = ( uuid.hiPart >> 16 ) & 0xFFFF;
	groups[2] = ( uuid.hiPart >> 00 ) & 0xFFFF;
	groups[3] = ( uuid.loPart >> 48 ) & 0xFFFF;
	groups[4] = ( uuid.loPart >> 00 ) & ( ( 0xFFFFFFFFul << 16 ) | 0xFFFF );
	Q_snprintfz( buffer, UUID_BUFFER_SIZE, format, groups[0], groups[1], groups[2], groups[3], groups[4] );
	return buffer;
}

/*
 * ============================================================================
 *
 * CLIENT RATING - common for ALL modules
 *
 * ============================================================================
 */

/*
 * Probability calculation
        Given player A with [skill Sa, uncertainity Ca] and player B with [skill Sb, uncertainity Cb] and T factor (see above)
        we calculate the probability P with this formula

            x = Sa - Sb
            d = T + T * Ca + T * Cb
            P = 1.0 / ( 1.0 + exp( -x*1.666666 / d ) )

        x is the skill difference, d is the normalization factor that includes uncertainity. The value of d specifies the
        "deepness" of the probability curve, larger d produces curve that is more flat in the vertical axis and smaller d
        gives a sharper transition thus showing that with smaller uncertainity the probability curve is more "sure"
        about the estimation.
 */
#if 0
static float * rating_getExpectedList( clientRating_t *list, int listSize ) {
	return NULL;
}
#endif

// returns the given rating or NULL
clientRating_t *Rating_Find( clientRating_t *ratings, const char *gametype ) {
	clientRating_t *cr = ratings;

	while( cr != NULL && strcmp( gametype, cr->gametype ) != 0 )
		cr = cr->next;

	return cr;
}

// as above but find with an ID
clientRating_t *Rating_FindId( clientRating_t *ratings, mm_uuid_t id ) {
	clientRating_t *cr = ratings;

	while( cr != NULL && !Uuid_Compare( cr->uuid, id ) )
		cr = cr->next;

	return cr;
}

// detaches given rating from the list, returns the element and sets the ratings argument
// to point to the new root. Returns NULL if gametype wasn't found
clientRating_t *Rating_Detach( clientRating_t **list, const char *gametype ) {
	clientRating_t *cr = *list, *prev = NULL;

	while( cr != NULL && strcmp( gametype, cr->gametype ) != 0 ) {
		prev = cr;
		cr = cr->next;
	}

	if( cr == NULL ) {
		return NULL;
	}

	if( prev == NULL ) {
		// detaching the root element
		*list = cr->next;
	} else {
		// detaching it from the middle
		prev->next = cr->next;
	}

	cr->next = NULL;
	return cr;
}

// detaches given rating from the list, returns the element and sets the ratings argument
// to point to the new root. Returns NULL if gametype wasn't found
clientRating_t *Rating_DetachId( clientRating_t **list, mm_uuid_t id ) {
	clientRating_t *cr = *list, *prev = NULL;

	while( cr != NULL && !Uuid_Compare( cr->uuid, id ) ) {
		prev = cr;
		cr = cr->next;
	}

	if( cr == NULL ) {
		return NULL;
	}

	if( prev == NULL ) {
		// detaching the root element
		*list = cr->next;
	} else {
		// detaching it from the middle
		prev->next = cr->next;
	}

	cr->next = NULL;
	return cr;
}

// head-on probability
float Rating_GetProbabilitySingle( clientRating_t *single, clientRating_t *other ) {
	float x, d;

	x = single->rating - other->rating;
	d = MM_DEFAULT_T + MM_DEFAULT_T * single->deviation + MM_DEFAULT_T * other->deviation;

	return LogisticCDF( x * 1.666666666f / d );
}

// returns a value between 0-1 for single clientRating against list of other clientRatings
// if single is on the list, it is ignored for the calculation
float Rating_GetProbability( clientRating_t *single, clientRating_t *list ) {
	float accum;
	int count;

	accum = 0.0;
	count = 0;

	while( list ) {
		accum += Rating_GetProbabilitySingle( single, list );
		count++;
		list = list->next;
	}

	if( count ) {
		accum = accum / (float)count;
	}

	return accum;
}

// TODO: Teams probability
// TODO: balanced team making
// TODO: find best opponent
// TODO: find best pairs

// create an average clientRating out of list of clientRatings
void Rating_AverageRating( clientRating_t *out, clientRating_t *list ) {
	float raccum, daccum;
	int count;

	raccum = daccum = 0.0;
	count = 0;

	while( list ) {
		raccum += list->rating;
		daccum += list->deviation;
		count++;
		list = list->next;
	}

	if( count ) {
		out->rating = raccum / (float)count;
		out->deviation = daccum / (float)count;
	} else {
		out->rating = MM_RATING_DEFAULT;
		out->deviation = MM_DEVIATION_DEFAULT;
	}
}
