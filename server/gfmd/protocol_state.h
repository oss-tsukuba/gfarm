/*
 * The state-transition-logic of struct protocol_state is described in gfmd.c,
 * although the storage for struct protocol_state is provided by struct peer.
 */

struct compound_state {
	/*
	 * which part of a COMPOUND block we are currently parsing:
	 *   GFARM_ERR_NO_ERROR: main part of a COMPOUND block
	 *   otherwise: GFM_PROTO_COMPOUND_ON_ERROR part
	 */
	gfarm_error_t current_part;

	/*
	 * an error at current level of a COMPOUND block.
	 *   GFARM_ERR_NO_ERROR: no error in this block for now.
	 */
	gfarm_error_t cause;

	/*
	 * true, if current part of this COMPOUND block must be skipped.
	 */
	int skip;

	/*
	 * XXX
	 * peer_fdpair must belong to struct compound_state.
	 * But because we don't allow nesting of COMPOUND blocks for now,
	 * we don't have to do that.
	 */
};

struct protocol_state {
	int nesting_level; /* 0: toplevel, or 1: inside COMPOUND */

	/*
	 * We don't allow nesting of a COMPOUND block for now,
	 * thus, only one state is enough.
	 */
	struct compound_state cs;
};
