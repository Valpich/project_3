
#include "fs.h"
#include "super.h"
#include "buf.h"
#include "fs_repair.h"

#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <minix/config.h>
#include <minix/const.h>
#include <minix/type.h>
#include <minix/u64.h>
#include "const.h"
#include "inode.h"
#include "type.h"
#include "mfsdir.h"
#include <minix/fslib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <a.out.h>
#include <dirent.h>


/* Defines */
#define EXIT_OK                    0
#define EXIT_USAGE                 1
#define EXIT_UNRESOLVED            2
#define EXIT_ROOT_CHANGED          4
#define EXIT_CHECK_FAILED          8
#define EXIT_SIGNALED             12
#define INDCHUNK	((int) (CINDIR * ZONE_NUM_SIZE))
#define BLOCK_SIZE 4096
#define V1_NR_DZONES       7    /* # direct zone numbers in a V1 inode */
#define V1_NR_TZONES       9    /* total # zone numbers in a V1 inode */
#define V2_NR_DZONES       7    /* # direct zone numbers in a V2 inode */
#define V2_NR_TZONES      10    /* total # zone numbers in a V2 inode */
#define BITMAP_CHUNKS (BLOCK_SIZE/usizeof (bitchunk_t))
#define MAX_ZONES (V2_NR_DZONES+V2_INDIRECTS(BLOCK_SIZE)+(long)V2_INDIRECTS(BLOCK_SIZE)*V2_INDIRECTS(BLOCK_SIZE))
#define btoa64(b)   (mul64u(b, BLOCK_SIZE))

/* Global variables */
bitchunk_t *imap_disk;			 /* imap from the disk */
bitchunk_t *zmap_disk;			 /* zmap from the disk */
static struct super_block *sb;   /* super block */
unsigned int WORDS_PER_BLOCK;    /* # words in a block */
unsigned int BLK_SIZE;			 /* block size */
int NATIVE;
bit_t ORIGIN_IMAP;		 		 /* sb->s_isearch */
bit_t ORIGIN_ZMAP;		 		 /* sb->s_zsearch */
zone_t  FIRST;					 /* first data zone */
block_t BLK_IMAP;			 	 /* starting block for imap */
block_t BLK_ZMAP;			 	 /* starting block for zmap */
block_t BLK_ILIST;			 	 /* starting block for inode table */
unsigned int N_IMAP;			 /* # blocks used for imap */
unsigned int N_ZMAP;			 /* # blocks used for zmap */
unsigned int N_ILIST;			 /* # blocks used for inode table */
unsigned int NB_INODES;			 /* # inodes */
unsigned int NB_ZONES;			 /* # zones */
unsigned int NB_USED = 0;		 /* # used (zones or inodes) */
unsigned int NB_INODES_USED = 0; /* # zones used (from IMAP) */
unsigned int NB_ZONES_USED_Z = 0;/* # zones used (from ZMAP) */
unsigned int NB_ZONES_USED_I = 0;/* # zones used (from IMAP) */
int repair    = 0;
int markdirty = 0;
int type = 0;

int file_descriptor;
dev_t dev;
char * dev_name;
char * rwbuf;            /* one block buffer cache */
block_t thisblk;       /* block in buffer cache */
unsigned int chunk_size = -1;


/*===========================================================================*
 *              print_inode               *
 *===========================================================================*/
void print_inode(ino)
struct inode * ino;
{
    printf("file type, protection, etc: %d .\n", ino->i_mode);
    printf("how many links to this file: %d .\n",ino->i_nlinks);
    printf("user id of the file's owner: %d .\n",ino->i_uid);
    printf("group number: %d .\n",ino->i_gid);
    printf("current file size in bytes: %d .\n",ino->i_size);
    printf("time of last access (V2 only): %d .\n",ino->i_atime);
    printf("when was file data last changed: %d .\n",ino->i_mtime);
    printf("when was inode itself changed (V2 only): %d .\n",ino->i_ctime);
    int i = 0;
    for(i = 0; i<V2_NR_TZONES ;i++){
        if(ino->i_zone[i] != 0)printf("zone numbers for direct, ind, and dbl ind: %d .\n",ino->i_zone[i]);
    }
    printf("The following items are not present on the disk.\n");
    printf("which device is the inode on: %d .\n",ino->i_dev);
    printf("inode number on its (minor) device: %d.\n",ino->i_num);
    printf("# times inode used; 0 means slot is free: %d .\n",ino->i_count);
    printf("# direct zones (Vx_NR_DZONES) : %d .\n",ino->i_ndzones);
    printf("# indirect zones per indirect block: %d .\n",ino->i_nindirs);
    printf("CLEAN or DIRTY: %d .\n",ino->i_dirt);
    printf("set on LSEEK, cleared on READ/WRITE: %d .\n",ino->i_seek);
    printf("the ATIME, CTIME, and MTIME bits are here: %d .\n",ino->i_update);
}

/*===========================================================================*
 *              print_super_block               *
 *===========================================================================*/
void print_super_block(sp)
struct super_block * sp;
{
    puts("Super block is:");
    printf("# usable inodes on the minor device: %d .\n", sp->s_ninodes);
    printf("total device size, including bit maps etc: %d .\n",sp->s_nzones);
    printf("# of blocks used by inode bit map: %d .\n",sp->s_imap_blocks);
    printf("# of blocks used by zone bit map: %d .\n",sp->s_zmap_blocks);
    printf("number of first data zone: %d .\n",sp->s_firstdatazone);
    printf("log2 of blocks/zone: %d .\n",sp->s_log_zone_size);
    printf("maximum file size on this device: %d .\n",sp->s_max_size);
    printf("magic number to recognize super-blocks: %d .\n",sp->s_magic);
    printf("try to avoid compiler-dependent padding: %d .\n",sp->s_pad2);
    printf("number of zones (replaces s_nzones in V2): %d .\n",sp->s_zones);
    printf("The following items are only used when the super_block is in memory.\n");
    printf("precalculated from magic number: %d .\n",sp->s_inodes_per_block);
    printf("whose super block is this? %d.\n",sp->s_dev);
    printf("set to 1 iff file sys mounted read only: %d .\n",sp->s_rd_only);
    printf("set to 1 iff not byte swapped file system: %d .\n",sp->s_native);
    printf("file system version, zero means bad magic: %d .\n",sp->s_version);
    printf("# direct zones in an inode: %d .\n",sp->s_ndzones);
    printf("# indirect zones per indirect block: %d .\n",sp->s_nindirs);
    printf("inodes below this bit number are in use: %d .\n",sp->s_isearch);
    printf("all zones below this bit number are in use: %d .\n",sp->s_zsearch);
    puts("End of super block printing");
}

/*===========================================================================*
 *				init_global		     *
 *===========================================================================*/
void init_global()
{
    /* Initialize all global variables for convenience of names */
    BLK_SIZE        = sb->s_block_size;
    FIRST       	= sb->s_firstdatazone;
    NB_INODES		= sb->s_ninodes;
    NB_ZONES		= sb->s_zones;
    N_IMAP 			= sb->s_imap_blocks;
    N_ZMAP 			= sb->s_zmap_blocks;
    N_ILIST			= (sb->s_ninodes + V2_INODES_PER_BLOCK(BLK_SIZE)-1) / V2_INODES_PER_BLOCK(BLK_SIZE);
    ORIGIN_IMAP		= sb->s_isearch;
    ORIGIN_ZMAP		= sb->s_zsearch;
    NATIVE			= sb->s_native;
    BLK_IMAP 		= 2;
    BLK_ZMAP 		= BLK_IMAP + N_IMAP;
    BLK_ILIST 		= BLK_ZMAP + N_ZMAP;
    WORDS_PER_BLOCK = BLOCK_SIZE / (int)sizeof(bitchunk_t);
    thisblk = NO_BLOCK;
}

/*===========================================================================*
 *              bitmapsize2          *
 *===========================================================================*/
static int bitmapsize2(nr_bits, blk_size)
bit_t nr_bits;
size_t blk_size;
{
    block_t nr_blocks;
    nr_blocks = nr_bits / FS_BITS_PER_BLOCK(blk_size);
    if (nr_blocks * FS_BITS_PER_BLOCK(blk_size) < nr_bits)
        ++nr_blocks;
    return(nr_blocks);
}

/*===========================================================================*
 *              check_super_block          *
 *===========================================================================*/
void check_super_block(sb)
struct super_block *sb;
{
    register int n;
    register off_t maxsize;
    n = bitmapsize2((bit_t) sb->s_ninodes + 1, BLOCK_SIZE);
    if (sb->s_magic != SUPER_V2 && sb->s_magic != SUPER_V3){
        fatal("bad magic number in super block");
    }else{
        puts("super block magic number is correct.");
    }
    if (sb->s_imap_blocks < n) {
        printf("need %d bocks for inode bitmap; only have %d\n", n, sb->s_imap_blocks);
        fatal("too few imap blocks");
    }else{
        puts("super block s_imap_blocks size is correct.");
    }
    if (sb->s_imap_blocks != n) {
        printf("warning: expected %d imap_block%s", n, "s");
        printf(" instead of %d\n", sb->s_imap_blocks);
    }else{
        puts("super block s_imap_blocks size is correct.");
    }
    n = bitmapsize2((bit_t) sb->s_zones, BLOCK_SIZE);
    if (sb->s_zmap_blocks < n) {
        fatal("too few zmap blocks");
    }else{
        puts("super block s_zmap_blocks size is correct.");
    }
    if (sb->s_zmap_blocks != n) {
        printf("warning: expected %d zmap_block%s", n, "s");
        printf(" instead of %d\n", sb->s_zmap_blocks);
    }
    if (sb->s_log_zone_size >= 8 * sizeof(block_t)){
        fatal("log_zone_size too large");
    }else{
        puts("super block s_log_zone_size is correct.");
    }
    if (sb->s_log_zone_size > 8) {
        printf("warning: large log_zone_size (%d)\n", sb->s_log_zone_size);
    }else{
        puts("super block s_log_zone_size is not large.");
    }
    sb->s_firstdatazone = (BLK_ILIST + N_ILIST +  ((int)((block_t) (1) << sb->s_log_zone_size)) - 1) >> sb->s_log_zone_size;
    if (sb->s_firstdatazone_old != 0) {
        if (sb->s_firstdatazone_old >= sb->s_zones){
            fatal("first data zone too large");
        }else{
            puts("super block s_firstdatazone is not too large.");
        }
        if (sb->s_firstdatazone_old < sb->s_firstdatazone){
            fatal("first data zone too small");
        }else{
            puts("super block s_firstdatazone is not too small.");
        }
        if (sb->s_firstdatazone_old != sb->s_firstdatazone) {
            printf("warning: expected first data zone to be %u ", sb->s_firstdatazone);
            printf("instead of %u\n", sb->s_firstdatazone_old);
            sb->s_firstdatazone = sb->s_firstdatazone_old;
        }else{
            puts("super block s_firstdatazone_old is correct.");
        }
    }
    maxsize = MAX_FILE_POS;
    if (((maxsize - 1) >> sb->s_log_zone_size) / BLOCK_SIZE >= MAX_ZONES){
        maxsize = ((long) MAX_ZONES * BLOCK_SIZE) << sb->s_log_zone_size;
    }
    if(maxsize <= 0){
        maxsize = LONG_MAX;
    }
    if (sb->s_max_size != maxsize) {
        printf("warning: expected max size to be %d ", maxsize);
        printf("instead of %d\n", sb->s_max_size);
    }else{
        puts("super block s_max_size is correct.");
    }
    if(sb->s_flags & MFSFLAG_MANDATORY_MASK) {
        fatal("unsupported feature bits");
    }else{
        puts("super block s_flags is correct.");
    }
}

/*===========================================================================*
 *              iterate_bitchunk          *
 *===========================================================================*/
int iterate_bitchunk(bitmap, nblk, list, type)
bitchunk_t *bitmap;
int nblk;
int * list;
int type;
{
    int j;
    NB_USED = 0;
    char * chunk;
    for(j=0; j<FS_BITMAP_CHUNKS(BLK_SIZE)*nblk; ++j){
        chunk = int2binstr(bitmap[j]);
        int k = 0;
        int u = 0;
        for (k = strlen(chunk) -1; k >= 0 ; k--) {
            if(chunk[k] == '1'){
                list[NB_USED] = u;
                NB_USED++;
            }
            u++;
        }
    }
    if(repair == 0){
        int v = 0;
        if(type == IMAP){
            printf("Used inodes in the inode bitmap are:\n");
            for(v = 0; v< NB_USED;v++){
                printf("inode #%d is used\n", list[v]);
            }
        }
        if(type == ZMAP){
            printf("Used inode in the zone bitmap are:\n");
            for(v = 0; v< NB_USED;v++){
                printf("inode #%d is used\n", list[v]);
            }
        }
        sleep(5);
    }
    return NB_USED;
}

/*===========================================================================*
 *				get_list_used	     *
 *===========================================================================*/
int * get_list_used(bitmap, type)
bitchunk_t *bitmap;
int type;
{
    int * list;
    int nblk;
    int tot;
    if (type == IMAP){
        nblk = N_IMAP;
        tot  = NB_INODES;
        list = malloc(sizeof(int)*NB_INODES);
    }
    else if (type==ZMAP)  {
        nblk = N_ZMAP;
        tot  = NB_ZONES;
        list = malloc(sizeof(int)*NB_ZONES);
    }
    NB_USED = iterate_bitchunk(bitmap, nblk, list, type);
    if (type == IMAP)    NB_INODES_USED  = NB_USED;
    else if (type==ZMAP) NB_ZONES_USED_Z = NB_USED;
    if(repair == 0)
        printf("Used: %d / %d \n", NB_USED, tot);
    return list;
}

/*===========================================================================*
 *				get_list_blocks_from_inodes			     *
 *===========================================================================*/
int * get_list_blocks_from_inodes(inodes)
int * inodes;
{
    int* list = malloc(sizeof(int)*NB_INODES*V2_NR_DZONES);;
    int used_zones = 0;
    int indirect_zones = 0;
    int double_indirect_zones = 0;
    register struct inode *rip;
    int* zones;
    zone_t *indir, *double_indir;
    int i, j, k = 0;    
    for (i = 0; i != NB_INODES_USED; ++i){
        if ((rip = get_inode(dev, inodes[i])) == NULL){
            fatal("Inode not found\n");
            return NULL;
        }
        print_inode(rip);
        if (rip->i_nlinks == NO_LINK){
            continue;
        }
        zones = (int *) rip->i_zone;        
        for (j = 0; j < V2_NR_DZONES; ++j){
            if (zones[j] == 0) break;
            list[used_zones] = zones[j];
            used_zones++;
        }
        if (zones[j] == 0) {
            put_inode(rip); 
            continue;
        }
        indir = (zone_t *) check_indir(zones[j]);
        for (k = 0; k < BLK_SIZE/2; ++k){
            if (indir[k] == 0) break;
            list[used_zones] = indir[k];
            used_zones++;
            indirect_zones++;
        }
        free(indir);
        j++;
        if (zones[j] == 0) {
            put_inode(rip); 
            continue; 		
        }
        double_indir = (zone_t *) check_double_indir(zones[j]);
        for (int k = 0; k < BLK_SIZE/2*BLK_SIZE/2; ++k){
            if (double_indir[k] == 0) break;
            list[used_zones] = double_indir[k];
            used_zones++;
            double_indirect_zones++;
        }
        free(double_indir);
        put_inode(rip);
    }
    if(repair == 0){
        puts("Printing the used zones:");
        sleep(2);
        for (int k = 0; k < used_zones; ++k){
            printf("zone # is %d.\n", list[k]);
        }
        printf("Number of used zones:            %d\n", used_zones);
        printf("Number of indirect zones:        %d\n", indirect_zones);
        printf("Number of double indirect zones: %d\n", double_indirect_zones);
    }
    sleep(5);
    NB_ZONES_USED_I = used_zones;
    return list;
}

/*===========================================================================*
 *				check_indir		     *
 *===========================================================================*/
int * check_indir(zno)
zone_t zno;
{
    struct buf *buf;
    zone_t *indir;
    int used_zones = 0;
    int l = 0;
    int *list = calloc(sizeof(int), BLK_SIZE/2);    
    if (zno == 0) return NULL;
    buf = get_block(dev, zno, 0);
    indir = b_v2_ind(buf);   
    for (l = 0; l < BLK_SIZE/2; ++l){
        if (indir[l] == 0) break;
        list[used_zones] = indir[l];
        used_zones++;
    }    
    return list;
}

/*===========================================================================*
 *				check_double_indir		     *
 *===========================================================================*/
int * check_double_indir( zno)
zone_t zno;
{
    struct buf *buf;
    zone_t *indir, *double_indir;
    int *list = calloc(sizeof(int), BLK_SIZE/2*BLK_SIZE/2);
    int used_zones = 0;
    if (zno == 0) return NULL;
    buf = get_block(dev, zno, 0);
    double_indir = b_v2_ind(buf);    
    for (int i = 0; i < BLK_SIZE/2; ++i){
        if (double_indir[i] == 0) break;
        indir = (zone_t *) check_indir(double_indir[i]);
        if (indir == NULL) return NULL;
        for (int j = 0; j < BLK_SIZE/2; ++j){
            list[used_zones] = indir[j];
            used_zones++;
        }
    }
    free(indir);
    return list;
}

/*===========================================================================*
 *				get_bitmap	         		*
 *===========================================================================*/
void get_bitmap(bitmap, type)
bitchunk_t * bitmap;
int type;
{
    block_t bno;
    int nblk;
    register int i;
    register bitchunk_t *p;
    register struct buf *bp;
    if (type == IMAP){
        bno  = BLK_IMAP;
        nblk = N_IMAP;
    }else if (type == ZMAP) {
        bno  = BLK_ZMAP;
        nblk = N_ZMAP;
    }
    p = bitmap;
    for (i = 0; i < nblk; i++, bno++, p += FS_BITMAP_CHUNKS(BLK_SIZE)){
        bp = get_block(dev, bno, 0);
        for (int j = 0; j < FS_BITMAP_CHUNKS(BLK_SIZE); ++j){
            p[j]  = b_bitmap(bp)[j];
        }
    }
}

/*===========================================================================*
 *				print_bitmap	     		*
 *===========================================================================*/
void print_bitmap(bitmap)
bitchunk_t * bitmap;
{
    int nblk;
    if (type == IMAP){
        nblk = N_IMAP;
        puts("Printing inode bitmap!");
    }else if(type == ZMAP){
        nblk = N_ZMAP;
        puts("Printing zone bitmap!");
    }
    for (int j = 0; j < FS_BITMAP_CHUNKS(BLK_SIZE)*nblk; ++j){
        printf("%s\n", int2binstr(bitmap[j]));
    }
    puts("End of bitmap printing.");
}

/*===========================================================================*
 *              bitmap_to_int_array                  *
 *===========================================================================*/
void bitmap_to_int_array(bitmap, nblk, output)
bitchunk_t * bitmap;
int nblk;
int * output;
{
    int j;
    char * chunk;
    int test = 0;
    for(j=0; j<FS_BITMAP_CHUNKS(BLK_SIZE)*nblk; ++j){
        chunk = int2binstr(bitmap[j]);
        int k = 0;
        int u = 0;
        for (k = strlen(chunk) -1; k >= 0 ; k--) {
            if(chunk[k] == '1'){
                output[test] = 1;
            }else{
                output[test] = 0;
            }
            u++;
            test++;
        }
    }
}

/*===========================================================================*
 *				int2binstr		     		*
 *===========================================================================*/
char * int2binstr(i)
unsigned int i;
{
    size_t bits = sizeof(unsigned int) * CHAR_BIT;
    char * str = malloc(bits + 1);
    if(!str) return NULL;
    str[bits] = 0; 
    chunk_size = bits;
    unsigned u = *(unsigned *)&i;
    for(; bits--; u >>= 1)
        str[bits] = u & 1 ? '1' : '0';  
    return str;
}

/*===========================================================================*
 *				alloc			     		*
 *===========================================================================*/
char * alloc(nelem, elsize)
unsigned nelem, elsize;
{
    char *p;
    if ((p = (char *)malloc((size_t)nelem * elsize)) == 0) {
        fatal("out of memory!");
    }
    memset((void *) p, 0, (size_t)nelem * elsize);
    return(p);
}

/*===========================================================================*
 *				alloc_bitmap	     		*
 *===========================================================================*/
bitchunk_t * alloc_bitmap(nblk)
int nblk;
{
    register bitchunk_t *bitmap;
    bitmap = (bitchunk_t *) alloc((unsigned) nblk, BLK_SIZE);
    *bitmap |= 1;
    return bitmap;
}

/*===========================================================================*
 *				free_bitmap		     		*
 *===========================================================================*/
void free_bitmap(p)
bitchunk_t *p;
{
    free((char *) p);
}

/*===========================================================================*
 *              compare_bitmaps          *
 *===========================================================================*/
int compare_bitmaps(bitmap, bitmap2, nblk, list)
bitchunk_t * bitmap;
bitchunk_t * bitmap2;
int nblk;
int * list;
{
    int j;
    int corrupted = 0;
    char * chunk;
    char * chunk2;
    for(j=0; j<FS_BITMAP_CHUNKS(BLK_SIZE)*nblk; ++j){
        chunk = int2binstr(bitmap[j]);
        chunk2 = int2binstr(bitmap2[j]);
        int k = 0;
        int u = 0;
        for (k = strlen(chunk) -1; k >= 0 ; k--) {
            if(chunk[k] != chunk2[k]){
                list[corrupted] = u;
                corrupted++;
            }
            u++;
        }
    }
    if(corrupted != 0){
        printf("Found %d corrupted data between the two bitmap.\n", corrupted);
        int v = 0;
        for(v=0; v<corrupted;v++){
            printf("The inode %d has two different values.\n", list[v]);
        }
    }else{
        puts("No difference between bitmaps found.");
    }
    return corrupted;
}

/*===========================================================================*
 *              devio          *
 *===========================================================================*/
void devio(bno, dir)
block_t bno;
int dir;
{
    int r;
    if(!BLOCK_SIZE) fatal("devio() with unknown block size");
    if (dir == READING && bno == thisblk) return;
    thisblk = bno;
    #if 1
    printf("%s at block %5d\n", dir == READING ? "reading " : "writing", bno);
    #endif
    printf("dev is %d SEEK_SET is  %d.\n", dev, SEEK_SET);
    printf("file_descriptor is %d.\n",file_descriptor);
    if(file_descriptor != -1 ){
        printf("file_descriptor open is %d\n", file_descriptor);
        r= lseek64(file_descriptor, btoa64(bno), SEEK_SET, NULL);
        if (r != 0)
            fatal("lseek64 failed");
        if (dir == READING) {
            if (read(dev, rwbuf, BLOCK_SIZE) == BLOCK_SIZE)
                return;
        } else {
            if (write(dev, rwbuf, BLOCK_SIZE) == BLOCK_SIZE){
                return;
            }
        }
        printf("%s: can't %s block %ld (error = 0x%x)\n", "fs_repair", dir == READING ? "read" : "write", (long) bno, errno);
        if (dir == READING) {
            printf("Continuing with a zero-filled block.\n");
            memset(rwbuf, 0, BLOCK_SIZE);
            return;
        }
        fatal("");
    }else{
        printf("file_descriptor is not open.\n");
    }
}

/*===========================================================================*
 *              devwrite          *
 *===========================================================================*/
void devwrite(block, offset, buf, size)
long block;
long offset;
char *buf;
int size;
{
  if(!BLOCK_SIZE) fatal("devwrite() with unknown block size");
  if (offset >= BLOCK_SIZE)
  {
    block += offset/BLOCK_SIZE;
    offset %= BLOCK_SIZE;
}
if (size != BLOCK_SIZE) devio(block, READING);
memmove(&rwbuf[offset], buf, size);
devio(block, WRITING);
}

/*===========================================================================*
 *              dumpbitmap          *
 *===========================================================================*/
void dumpbitmap(bitmap, bno, nblk)
bitchunk_t *bitmap;
block_t bno;
int nblk;
{
  register int i;
  register bitchunk_t *p = bitmap;

  for (i = 0; i < nblk; i++, bno++, p += WORDS_PER_BLOCK){
    devwrite(bno, 0, (char *) p, BLOCK_SIZE);
}
}

/*===========================================================================*
 *				fatal			     		*
 *===========================================================================*/
void fatal(s)
char * s;
{
    printf("%s\nfatal\n", s);
    exit(EXIT_CHECK_FAILED);
}

/*===========================================================================*
 *              fs_directory_bitmap_walker                       *
 *===========================================================================*/
int fs_directory_bitmap_walker()
{
    puts("fs_directory_bitmap_walker");
    repair = 0;
    return 0;
}

/*===========================================================================*
 *              fs_inode_bitmap_walker               *
 *===========================================================================*/
int fs_inode_bitmap_walker()
{
    int * list_blocks;
    int * list_inodes;
    repair = 0;
    puts("fs_inode_bitmap_walker started");
    dev = fs_m_in.REQ_DEV;
    type = IMAP;
    printf("Loading super block in the %u device.\n",dev);
    sb = get_super(dev);
    read_super(sb);
    print_super_block(sb);
    sleep(2);
    init_global();
    imap_disk = alloc_bitmap(N_IMAP);
    puts("Loading inode bitmap.");
    get_bitmap(imap_disk, IMAP);
    list_inodes = get_list_used(imap_disk, IMAP);
    if ((list_blocks = get_list_blocks_from_inodes(list_inodes)) == NULL){
        puts("fs_inode_bitmap_walker ended with failure");
        free_bitmap(imap_disk);
        return -1;
    }
    free(list_inodes);
    free(list_blocks);
    free_bitmap(imap_disk);
    puts("fs_inode_bitmap_walker ended with success");
    return 0;
}

/*===========================================================================*
 *              fs_zone_bitmap_walker                *
 *===========================================================================*/
int fs_zone_bitmap_walker()
{
    int * list;
    repair = 0;
    puts("fs_zone_bitmap_walker started");
    dev = fs_m_in.REQ_DEV;
    printf("Loading super block in the %u device.\n",dev);
    type = ZMAP;
    sb = get_super(dev);
    read_super(sb);
    print_super_block(sb);
    sleep(2);
    init_global();
    zmap_disk = alloc_bitmap(N_ZMAP);
    puts("Loading zone bitmap.");
    get_bitmap(zmap_disk, ZMAP);
    list = get_list_used(zmap_disk, ZMAP);
    free_bitmap(zmap_disk);
    free(list);
    puts("fs_zone_bitmap_walker ended with success");
    return 0;
}


/*===========================================================================*
 *              fs_recovery                 *
 *===========================================================================*/
int fs_recovery(void){
    puts("fs_recovery started");
    int * list;
    int * list_blocks;
    int * list_inodes;
    dev = fs_m_in.REQ_DEV;
    repair = 1;
    printf("Loading super block in the %u device.\n",dev);
    type = ZMAP;
    sb = get_super(dev);
    read_super(sb);
    init_global();
    zmap_disk = alloc_bitmap(N_ZMAP);
    get_bitmap(zmap_disk, ZMAP);
    list = get_list_used(zmap_disk, ZMAP);
    type = IMAP;
    imap_disk = alloc_bitmap(N_IMAP);
    get_bitmap(imap_disk, IMAP);
    list_inodes = get_list_used(imap_disk, IMAP);
    if ((list_blocks = get_list_blocks_from_inodes(list_inodes)) == NULL){
        puts("fs_recovery ended with failure");
        free_bitmap(zmap_disk);
        free_bitmap(imap_disk);
        return -1;
    }
    int nblk = N_ZMAP > N_IMAP ? N_IMAP : N_ZMAP;
    compare_bitmaps(zmap_disk, imap_disk, nblk, list);
    register struct inode *rip;
    int temp = 0;
    int max = 1024*1024*32;
    int * output_inode = calloc(max,sizeof(int));
    int iterate = 0;
    for(iterate = 0; iterate<max ; iterate++){
        output_inode[iterate] = -1;
    }
    for(int i = 0; i< (sb->s_ninodes);i++){
        if ((rip = get_inode(dev, i)) == NULL){
        }else{
            if(rip->i_nlinks>0){
                output_inode[temp] = 1;
            } else{
                output_inode[temp] = 0;
            }
            temp++;
        }
    }
    output_inode[0] = 1;
    printf("16 first inode list entries are:\n");
    for(int sd = 0 ;sd < 16; sd++){
        printf("inode is %d value is %d\n", sd,output_inode[sd] );
    }
    int * inode_bitmap_as_int_array = calloc(FS_BITMAP_CHUNKS(BLK_SIZE)*N_IMAP*chunk_size +1, sizeof(int));
    inode_bitmap_as_int_array[FS_BITMAP_CHUNKS(BLK_SIZE)*N_IMAP*chunk_size] = 0;
    bitmap_to_int_array(imap_disk, N_IMAP, inode_bitmap_as_int_array);
    printf("inode_bitmap_as_int_array is %d #0 is %d, #1 is %d, #2 is %d.\n",(int)inode_bitmap_as_int_array, inode_bitmap_as_int_array[0], inode_bitmap_as_int_array[1], inode_bitmap_as_int_array[2]);
    int * zone_bitmap_as_int_array = calloc(FS_BITMAP_CHUNKS(BLK_SIZE)*N_ZMAP*chunk_size +1, sizeof(int));
    zone_bitmap_as_int_array[FS_BITMAP_CHUNKS(BLK_SIZE)*N_ZMAP*chunk_size] = 0;
    bitmap_to_int_array(zmap_disk, N_ZMAP, zone_bitmap_as_int_array);
    printf("zone_bitmap_as_int_array is %d #0 is %d, #1 is %d, #2 is %d.\n",(int)zone_bitmap_as_int_array, zone_bitmap_as_int_array[0], zone_bitmap_as_int_array[1], zone_bitmap_as_int_array[2]);
    output_inode[temp] = 0;
    printf("output_inode is %d #0 is %d, #1 is %d, #2 is %d.\n",(int)output_inode, output_inode[0], output_inode[1], output_inode[2]);
    fs_m_out.RES_DEV = (int) inode_bitmap_as_int_array;
    fs_m_out.RES_FILE_SIZE_HI = (int) zone_bitmap_as_int_array;
    fs_m_out.RES_NBYTES = N_IMAP;
    fs_m_out.RES_FILE_SIZE_LO = N_ZMAP;
    fs_m_out.RES_INODE_NR = (int) output_inode;
    unsigned int x = (unsigned int) temp; //or any other value it happens to be
    unsigned short high = (unsigned short)(x>>8);
    unsigned short low  = x & 0xff;
    fs_m_out.RES_GID = high;
    fs_m_out.RES_MODE = low;
    sleep(5);
    free_bitmap(zmap_disk);
    free_bitmap(imap_disk);
    puts("fs_recovery ended with success");
    return 1;
}

/*===========================================================================*
 *              damage_bitmap                 *
 *===========================================================================*/
void damage_bitmap(bitmap, nblk, type, number)
bitchunk_t *bitmap;
int nblk;
int type;
int number;
{
   int j;
   char * chunk;
   int u = 0;
   for(j=0; j<FS_BITMAP_CHUNKS(BLK_SIZE)*nblk; ++j){
    chunk = int2binstr(bitmap[j]);
    int modified = 0;
    int k = 0;
    for (k = strlen(chunk) -1; k >= 0 ; k--) {
        if(u == number){
            printf("chunk before is %s.\n", chunk);
            char * one = malloc(2*sizeof(char));
            char * two = malloc(2*sizeof(char));
            one[0] = '1';
            one[1] = 0;
            two[0] = chunk[k];
            two[1] = 0;
            if(strcmp (one, two) == 0){
                chunk[k] = '0';
                puts("set to 0");
            }else {
                chunk[k] = '1';
                puts("set to 1");
            }
            free(one);
            free(two);
            printf("chunk after is %s.\n", chunk);
            modified = 1;
            sleep(1);
        }
        u++;
    }
    if(modified == 1){
        char * pEnd;
        unsigned int update = strtol(chunk,&pEnd,2);
        printf("update is %d \n", update);
        sleep(1);
        bitmap[j] = update;
    }
}
}

/*===========================================================================*
 *              fs_damage                 *
 *===========================================================================*/
int fs_damage(void){
    puts("fs_damage started");
    int position = fs_m_in.m1_i1;
    int operation = fs_m_in.m1_i2;
    char * folder = fs_m_in.m1_p1;
    printf("fs damage requested for position #%d.\n", position);
    printf("fs damage requested for operation #%d.\n", operation);
    printf("fs damage requested for folder #%s.\n", folder);
    dev = fs_m_in.REQ_DEV;
    sb = get_super(dev);
    read_super(sb);
    init_global();
    if(!(rwbuf = malloc(BLOCK_SIZE))) fatal("couldn't allocate fs buf (1)");
    check_super_block(sb);
    if(operation == 1){
        repair = 1;
        printf("Loading super block in the %u device.\n",dev);
        type = ZMAP;
        zmap_disk = alloc_bitmap(N_ZMAP);
        get_bitmap(zmap_disk, ZMAP);
        type = IMAP;
        imap_disk = alloc_bitmap(N_IMAP);
        get_bitmap(imap_disk, IMAP); 
        damage_bitmap(imap_disk, N_IMAP, IMAP, position);
        printf("BLK_IMAP is %d N_IMAP is %d.\n",BLK_IMAP, N_IMAP);
        int * bitmap_as_int_array = calloc(FS_BITMAP_CHUNKS(BLK_SIZE)*N_IMAP*chunk_size +1, sizeof(int));
        bitmap_as_int_array[FS_BITMAP_CHUNKS(BLK_SIZE)*N_IMAP*chunk_size] = 0;
        bitmap_to_int_array(imap_disk, N_IMAP, bitmap_as_int_array);
        printf("bitmap_as_int_array is %d #0 is %d, #1 is %d, #2 is %d.\n",(int)bitmap_as_int_array, bitmap_as_int_array[0], bitmap_as_int_array[1], bitmap_as_int_array[2]);
        fs_m_out.RES_DEV = (int) bitmap_as_int_array;
        printf("src mfs is  %lu .\n",fs_m_out.RES_DEV);
        printf("N_IMAP is %d\n", N_IMAP);
        fs_m_out.RES_NBYTES = N_IMAP;
    }else if(operation == 2){
        repair = 1;
        printf("Loading super block in the %u device.\n",dev);
        type = ZMAP;
        zmap_disk = alloc_bitmap(N_ZMAP);
        get_bitmap(zmap_disk, ZMAP);
        damage_bitmap(zmap_disk, N_ZMAP, ZMAP, position);
        printf("BLK_ZMAP is %d N_ZMAP is %d.\n",BLK_ZMAP, N_ZMAP);
        int * bitmap_as_int_array = calloc(FS_BITMAP_CHUNKS(BLK_SIZE)*N_ZMAP*chunk_size +1, sizeof(int));
        bitmap_as_int_array[FS_BITMAP_CHUNKS(BLK_SIZE)*N_ZMAP*chunk_size] = 0;
        bitmap_to_int_array(zmap_disk, N_ZMAP, bitmap_as_int_array);
        printf("bitmap_as_int_array is %d #0 is %d, #1 is %d, #2 is %d.\n",(int)bitmap_as_int_array, bitmap_as_int_array[0], bitmap_as_int_array[1], bitmap_as_int_array[2]);
        fs_m_out.RES_DEV = (int) bitmap_as_int_array;
        printf("src mfs is  %lu .\n",fs_m_out.RES_DEV);
        printf("N_IMAP is %d\n", N_ZMAP);
        fs_m_out.RES_NBYTES = N_ZMAP;
    }
    free(rwbuf);
    puts("fs_damage ended with success");
    return 1;
}
