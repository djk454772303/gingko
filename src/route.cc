/*
 * route.cc
 *
 *  Created on: 2011-5-25
 *      Author: auxten
 */
#include "gingko.h"

GINGKO_OVERLOAD_S_HOST_EQ

extern s_host the_host;
extern s_host the_server;
const bool cmpByDistance(const s_host & h1, const s_host & h2) {
    //return abs(h1.port - 59999) > abs(h2.port - 59999);
    return host_distance(&the_host, &h1) < host_distance(&the_host, &h2);
}

extern pthread_rwlock_t blk_hostset_lock;
int get_blk_src(s_job * jo, int src_max, int64_t blk_num,
        vector<s_host> * h_vec) {
    u_int64_t max = MIN(src_max+1, (*jo->host_set).size());
    s_block * b;
    // count back blk to find the upper stream src
    // Note that, the src count may be more than the max
    (*h_vec).clear();
    while ((*h_vec).size() < max) {
        b = jo->blocks + blk_num;
        pthread_rwlock_rdlock(&blk_hostset_lock);
        if (b->host_set != NULL && (*(b->host_set)).size() != 0) {
            for (set<s_host>::iterator i = (*(b->host_set)).begin(); i
                    != (*(b->host_set)).end(); i++) {
                if (find((*h_vec).begin(), (*h_vec).end(), *i)
                        == (*h_vec).end()) {
                    (*h_vec).push_back(*i);
                }
            }
        }
        pthread_rwlock_unlock(&blk_hostset_lock);
        //printf("blk_num: %lld\n", blk_num);
        blk_num = prev_b(jo, blk_num);
    }
    //insert the_server
    (*h_vec).push_back(the_server);
    for (vector<s_host>::iterator i = (*h_vec).begin(); i != (*h_vec).end(); i++) {
        //printf("#####host in vec: %s %d\n", i->addr, i->port);
    }
    // sort the src by distance, cause we just need the first src_max+1
    sort((*h_vec).begin(), (*h_vec).end(), cmpByDistance);
    return (*h_vec).size();
}

/*
 * decide the final src from the host set generated by get_blk_src()
 * return block count downloaded during test the source speed
 */
int decide_src(s_job * jo, int src_max, int64_t blk_num,
        vector<s_host> * h_vec, s_host * h) {
    int num, host_i = 0;
    int64_t blk_i = 0;
    static struct timeval before_tv;
    struct timeval after_tv;
    int64_t diff = MAX_INT64;
    int64_t tmp;
    vector<s_host>::iterator fastest = (*h_vec).end();
    s_block * b;
    char * buf;
    /*
     * prepare the buf to recv data
     */
    if ((buf = (char *) malloc(BLOCK_SIZE)) == NULL) {
        perr("malloc for read buf of blocks_size failed");
        return 0;
    }
    for (vector<s_host>::iterator i = (*h_vec).begin(); i != (*h_vec).end(); i++) {
        //if the first host in the vector is myself, pass it
        if (host_distance(&the_host, &(*i)) == 0)
            continue;
        gettimeofday(&before_tv, NULL);
        num = getblock(&(*i), blk_num + blk_i, 1, 0 & ~W_DISK, buf);
        gettimeofday(&after_tv, NULL);
        b = jo->blocks + (blk_num + blk_i) % jo->block_count;
        if (num == 1 && digest_ok(buf, b)) {
            //fprintf(stderr, "digest_ok\n");
            if (writeblock(jo, (unsigned char *) buf, b) < 0) {
                return 0;
            } else {
                b->done = 1;
            }
            blk_i++;
            tmp = (after_tv.tv_sec - before_tv.tv_sec) * 1000000L
                    + after_tv.tv_usec - before_tv.tv_usec;
        } else {
            //if get no block, make its time longest
            //fprintf(stderr, "getblock ret: %d\n", num);
            tmp = MAX_INT64;
        }
        // make the_server SERV_TRANS_TIME_PLUS microseconds slower :p
        if (*i == the_server) {
            if (tmp != MAX_INT64)
                tmp += SERV_TRANS_TIME_PLUS;
        }
        //printf("time: %lld\n", tmp);
        if (diff > tmp) {
            diff = tmp;
            fastest = i;
        }
        if (++host_i == src_max) {
            //make i point to the "the_server"
            i = (*h_vec).end() - 2;
            continue;
        }
    }
    //get NO available src
    if (fastest == (*h_vec).end()) {
        return 0;
    }
    memcpy(h, &(*fastest), sizeof(s_host));
    //printf("choose %s:%d\n", h->addr, h->port);
    free(buf);
    return blk_i;
}
