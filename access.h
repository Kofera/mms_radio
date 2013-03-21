#ifndef ACCESS_H
#define ACCESS_H

struct access_t
{
  char *psz_access;
  char *psz_path;

  struct
  {
    unsigned int i_update;
    uint64_t i_size;
    uint64_t i_pos;
    bool b_eof;
    int i_title;
    int i_seekpoint;
  } info;
  
  //ssize_t (*pf_read)(access_t *, uint8_t *, size_t);
  block_t  *(*pf_block)(access_t*);

  access_sys_t *p_sys;
  decode_t *dc;
};

static inline void access_InitFields(access_t *p_a)
{
  p_a->info.i_update = 0;
  p_a->info.i_size = 0;
  p_a->info.i_pos = 0;
  p_a->info.b_eof = false;
  p_a->info.i_title = 0;
  p_a->info.i_seekpoint = 0;
}

#endif
