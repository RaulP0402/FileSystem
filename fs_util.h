
int rand_string(char *str, size_t size);
void set_bit(char *array, int index, char value);
char get_bit(char *array, int index);
int get_free_inode();
int set_free_inode(int inode);
int get_free_block();
int set_free_block(int block);
int format_timeval(struct timeval *tv, char *buf, size_t sz);