#define SBPE_TEST 1
#define SBPE_CHECKED_READ_TEST 1
#include "../src/sbpe.c"
#include <assert.h>
#include <sys/mman.h>

int main(void){
    chest_class=(void*)0x12345000;
    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    unsigned char *memory=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    assert(memory!=MAP_FAILED);assert(!mprotect(memory+page,page,PROT_NONE));
    struct WorldObject *object=(void*)memory;object->classptr=chest_class;
    struct ItemProperties item={0};item.type=49;void *items[]={&item};
    struct RepeatedPtrField field={items,1,1,1};int pos=-1;
    memcpy(memory+0x1b8,&field,sizeof(field));memcpy(memory+0x1e8,&pos,sizeof(pos));
    struct ChestItem rows[CHEST_ITEMS];struct ChestState state;
    assert(chest_state(object,&state));assert(chest_snapshot(object,rows)==1);assert(!strcmp(rows[0].name,"1 EC"));

    /* Same kind of noncanonical vtable observed in the real SC crash.
     * It must never be followed to vtable[-1]/type_info/name. */
    object->classptr=(void*)0x300007f9b8b33092ULL;
    assert(!chest_state(object,&state));assert(!chest_snapshot(object,rows));
    object->classptr=(void*)1;assert(!chest_snapshot(object,rows));
    object->classptr=NULL;assert(!chest_snapshot(object,rows));

    /* A live class pointer does not make its item array or item live. */
    object->classptr=chest_class;field.elements=(void**)(memory+page);
    memcpy(memory+0x1b8,&field,sizeof(field));assert(!chest_snapshot(object,rows));
    items[0]=memory+page;field.elements=items;memcpy(memory+0x1b8,&field,sizeof(field));
    assert(!chest_snapshot(object,rows));items[0]=&item;
    field.current_size=257;field.allocated_size=field.total_size=257;
    memcpy(memory+0x1b8,&field,sizeof(field));assert(!chest_snapshot(object,rows));

    /* Readable type tag followed by an inaccessible/truncated header. */
    struct WorldObject *edge=(void*)(memory+page-sizeof(void*));edge->classptr=chest_class;
    assert(!chest_state(edge,&state));assert(!chest_state((void*)(memory+page),&state));
    assert(!chest_state((void*)1,&state));assert(!chest_state(NULL,&state));

    /* The real overlay must also reject a retired candidate, preserve its
     * drawing state, and survive a stale player address. */
    struct ClientWorld world={0};struct WorldView view={0};struct WorldObject player={0};
    world.player=(void*)&player;int width=800,height=600;canvas_w=&width;canvas_h=&height;
    physical_w=width;physical_h=height;text_outline=2;collecting_source=2;
    nobjects=0;memset(seen_objects,0,sizeof(seen_objects));memset(seen_source,0,sizeof(seen_source));
    add_object((void*)(memory+page));collecting_source=0;chest_overlay(&world,&view);
    assert(text_outline==2&&width==800&&height==600);
    world.player=(void*)(memory+page);chest_overlay(&world,&view);assert(text_outline==2);

    assert(!munmap(memory,page*2));assert(!chest_snapshot(object,rows));
    puts("PASS: valid chest/items, crash-shaped invalid vtable, inaccessible/retired object, truncated header, bad item array/item, bounded count and overlay/player state; real checked Mach reads, no live game.");
}
