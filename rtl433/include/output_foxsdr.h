/** @file
    FoxSDR GPL helper: the one output handler of this helper.

    New file written for the FoxSDR helper (not part of upstream rtl_433).
    GPL-2.0-or-later, like the rest of the helper.

    Turns every decoded rtl_433 event (a data_t list) into one JSON Lines
    object of the FoxSDR helper pipe protocol version 1:

      {"t":<seconds>,"type":"rtl433", <rtl_433 fields...>,"text":"<summary>"}
*/

#ifndef INCLUDE_OUTPUT_FOXSDR_H_
#define INCLUDE_OUTPUT_FOXSDR_H_

#include <stdio.h>
#include "data.h"

/// Create the output. @p fp receives the JSON lines (flushed per line).
/// @p hide_ids replaces every identifier field by a short salted hash.
/// @p salt is the salt text for those hashes (may not be NULL when hide_ids is set).
data_output_t *foxsdr_output_create(FILE *fp, int hide_ids, char const *salt);

/// Number of event lines written so far.
unsigned foxsdr_output_events(data_output_t const *output);

/// Short stable hash text ("h" + 8 hex digits) of @p value under @p salt. Exposed for the tests.
void foxsdr_hash_id(char const *salt, char const *value, char out[16]);

/// Whether a field name is an identifier field (hidden by --hide-ids).
int foxsdr_is_id_key(char const *key);

/// Whether a field name carries raw payload bytes (dropped by --hide-ids, the ids can be read out of them).
int foxsdr_is_payload_key(char const *key);

#endif /* INCLUDE_OUTPUT_FOXSDR_H_ */
