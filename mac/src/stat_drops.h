/* Windows extra_info BOOSTS rule: highlight a drop only when at least one
 * tier it can grant exceeds the current base stat (equipment bonus excluded).
 * Mac offsets are verified against PlayerCharacter::applyBoost. */
_Static_assert(sizeof(struct RepeatedField_int)==16,"Mac repeated int layout");
_Static_assert(offsetof(struct RepeatedField_int,current_size)==8,"Mac repeated int count");
struct DropStat {const char *vid;size_t base,levels;};
static const struct DropStat drop_stats[]={
    {"loot-maxhealth",0x2c4,0x28},{"loot-maxammo",0x2cc,0x38},
    {"loot-damage",0x2dc,0x48},{"loot-armor",0x2e4,0x60},
    {"loot-mspeed",0x2ec,0x70},{"loot-jheight",0x2f4,0x80},
    {"loot-critchance",0x2fc,0x90},{"loot-critmult",0x304,0xa0}
};
static int needed_stat_drop(struct ClientWorld *cw,const char *vid){
    const struct DropStat *stat=NULL;int tier=0;
    for(size_t i=0;i<sizeof(drop_stats)/sizeof(*drop_stats);++i){
        size_t n=strlen(drop_stats[i].vid);
        if(!strncmp(vid,drop_stats[i].vid,n)&&vid[n]>='1'&&vid[n]<='9'&&!vid[n+1]){stat=&drop_stats[i];tier=vid[n]-'0';break;}
    }
    if(!stat||!cw||!cw->player)return 0;
    if(!in_list(class_name(cw->player),"PlayerCharacter Ancient Duskwing Fabricator Ironclad Toaster Wildfire"))return 0;
    const unsigned char *player=(const void*)cw->player,*desc=NULL;int current;
    struct RepeatedField_int levels;
    if(!read_memory(player+0x278,&desc,sizeof(desc))||!desc||
       !read_memory(player+stat->base,&current,sizeof(current))||current<0||
       !read_memory(desc+stat->levels,&levels,sizeof(levels))||!levels.elements||
       levels.current_size<2||levels.current_size>128||levels.total_size<levels.current_size||levels.total_size>1024)return 0;
    int n=tier<levels.current_size?tier:levels.current_size-1,values[9];
    if(!read_memory(levels.elements+1,values,(size_t)n*sizeof(*values)))return 0;
    for(int i=0;i<n;++i)if(values[i]>current)return 1;
    return 0;
}
