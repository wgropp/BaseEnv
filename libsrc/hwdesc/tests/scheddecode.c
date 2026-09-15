#include <stdio.h>
#include <ctype.h>

int BENVi_GetNodeSocketFromPolicy(const char *policy, int np, int rank,
				  const int nobjs[2],
				  int *node, int *socket, int *rinsock);
void printTest(FILE *fp, const char *policy, const int nobjs[], int np);

/* Program to test decoding a scheduler policy into node/socket */
int main(int argc, char **argv)
{
    int np, nobjs[2];
    const char *policy;

    nobjs[0] = 6;
    nobjs[1] = 2;

    np = nobjs[0]*nobjs[1]*5;
    policy = "B:B";
    printTest(stdout, policy, nobjs, np);

    np = nobjs[0]*nobjs[1]*5;
    policy = "B";
    printTest(stdout, policy, nobjs, np);

    np = nobjs[0]*nobjs[1]*5;
    policy = "B:C";
    printTest(stdout, policy, nobjs, np);

    np = nobjs[0]*nobjs[1]*5;
    policy = "B:C(2)";
    printTest(stdout, policy, nobjs, np);

    nobjs[0] = 6;
    nobjs[1] = 3;
    np = nobjs[0]*nobjs[1]*5;
    policy = "B:C(2)";
    printTest(stdout, policy, nobjs, np);

    nobjs[0] = 4;
    nobjs[1] = 3;
    np = nobjs[0]*nobjs[1]*5;
    printTest(stdout, policy, nobjs, np);

    nobjs[0] = 4;
    nobjs[1] = 3;
    np = nobjs[0]*nobjs[1]*5;
    policy = "C(3):C(2)";
    printTest(stdout, policy, nobjs, np);

    return 0;
}

void printTest(FILE *fp, const char *policy, const int nobjs[], int np)
{
    int r, n, s, rins, rc;
    fprintf(fp, "rank:(node,socket,rinsock) for %s with %d nodes and %d sock/node\n",
	    policy, nobjs[0], nobjs[1]);
    for (r=0; r<np; r++) {
	n = -1; s = -1;
	rc = BENVi_GetNodeSocketFromPolicy(policy, np, r, nobjs, &n, &s, &rins);
	if (rc != 0) {
	    fprintf(fp, "Error return %d for rank %d\n", rc, r);
	    break;
	}
	else
	    fprintf(fp, "%d:(%d,%d,%d)\n", r, n, s, rins);
    }
}

#if 1
static int GetBlocksizeFromString(const char *policy, int *loc, int defblock);

/* Given a scheduler policy for assigning tasks to nodes and sockets, as
   well as the rank of a process in MPI_COMM_WORLD and the number of nodes
   nobjs[0] and sockets/node nobjs[1], return the node and socket of that
   process
*/
int BENVi_GetNodeSocketFromPolicy(const char *policy, int np, int rank,
				  const int nobjs[2],
				  int *node, int *socket, int *rins)
{
    int loc, blocknode, blocksocket, nn;
    int k, kk, rs, remainder, rrank;

    /* Extract strategy from policy */
    loc = 0;
    blocknode = GetBlocksizeFromString(policy, &loc, np/nobjs[0]);
    if (policy[loc] == ':') {
	loc++;
	blocksocket = GetBlocksizeFromString(policy, &loc, np/nobjs[0]/nobjs[1]);
	if (policy[loc]) {
	    /* Expected null! */
	    return 1;
	}
    }
    else {
	/* Set a default for blocksocket */
	blocksocket = np/nobjs[0]/nobjs[1];
    }
    /* Confirm value values */
    if (blocknode < 1 || blocksocket < 1)
	return 1;

    if (rank == 0)
	printf("Determined blocknode = %d and blocksocket = %d\n",
	       blocknode, blocksocket);
    /*
     * Because we can have cyclic with different sizes on nodes and sockets,
     * we have to work out the assignment of ranks first to nodes and then
     * within nodes to sockets.
     *
     * node is given by (rank/blocknode) % nobjs[0]
     */
    nn = (rank / blocknode) % nobjs[0];
    *node = nn;
    /* First rank on this process is idx = nn * blocknode */
    /* Ranks on this node are idx, idx+1, idx+2, ... idx+blocknode-1,
       same + blocknode*nobjs[0], same + 2*blocknode*nobjs[0], ...,
       or
         rank = (nn + k nobjs[0])*blocknode + remainder,
       where remainder is in [0,blocknode) and k is the number of this group
       (note nn/nobjs[0] == 0) */
    k = (rank / blocknode - nn)/nobjs[0];
    /* Must distribute these according to blocksocket across the sockets,
       and blocksocket and blocknode may not have a common set of factors.
       Determine the relative number of this rank on the node (e.g., of
       the ranks on this node, which number is it, starting consequtively
       from 0 */
    remainder = rank - (nn + k * nobjs[0])*blocknode;
    rrank = k * blocknode + remainder;
    /* Socket is given by distributing the rrank according to the blocksocket
       cyclic distribution */
    *socket = (rrank / blocksocket) % nobjs[1];
    /* Ranks on the node can also be written as
       rrank = m * blocksocket + rs (for integer m)
       and the ranks on socket s are
       rrank = (s + kk*nobjs[1])*blocksocket + rs
       where this is the same rrank as above, but represented relative to
       the mapping across sockets. */
    kk = (rrank / blocksocket - *socket)/nobjs[1];
    rs = rrank - (*socket+kk*nobjs[1])*blocksocket;
    /* Distribute the kk blocks across the nobjs[1] sockets */
    *rins = kk * blocksocket + rs;

    return 0;
}
#endif
static int GetBlocksizeFromString(const char *policy, int *loc, int defblock)
{
    int l = *loc, block;
    if (policy[l] == 'B') {
	l++;
	block = defblock;
    }
    else if (policy[l] == 'C') {
	l++;
	if (policy[l] == '(') {
	    block = 0;
	    l++;
	    while (isdigit(policy[l])) {
		block = block * 10 + policy[l]-'0';
		l++;
	    }
	    if (policy[l] != ')') {
		return -1;
	    }
	    else
		l++;
	}
	else {
	    block = 1;
	}
    }
    else {
	return -1;
    }
    *loc = l;
    return block;
}

