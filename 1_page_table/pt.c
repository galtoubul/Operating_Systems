#include <stdbool.h>
#include "os.h"
#define NUM_OF_LEVELS 5
#define NINE_LSB_BITS_MASK 0x00000000000001ff
#define LSB_MASK 0x0000000000000001
#define ZERO_VALUE_OFFSET_MASK 0xfffffffffffff000
#define OFFSET_LEN 12
#define VPN_PART_LEN 9

// Returns the proper vpn part in according to the given level
int getPtInd(uint64_t  vpn, int lvl){
    for(int i = 0; i < NUM_OF_LEVELS - lvl; i++)
        vpn = vpn >> VPN_PART_LEN;
    return NINE_LSB_BITS_MASK & vpn;
}

// Returns true if the given pte is a valid mapping. Ow -> false
bool isValid(uint64_t pte){
    return pte & LSB_MASK;
}

void setValid(uint64_t* pt, int ptInd){
    pt[ptInd] += 1;
}

void setNotValid(uint64_t* pt, int ptInd) {
    pt[ptInd] -= 1;
}

/**
 * A function to create/destroy virtual memory mappings in a PT
 * @param pt - the PPN of the PT root (can assume that it was returned by alloc_page_frame
 * @param vpn - the VPN the caller wishes to map/unmap
 * @param ppn - can be on of: (1) NO_MAPPING -> vpn's mapping should be destroyed (if exist)
 *                            (2) the PPN that vpn should be mapped to
 */
void page_table_update(uint64_t pt, uint64_t vpn, uint64_t ppn){
    uint64_t *ptPtr = (uint64_t*)phys_to_virt(pt << OFFSET_LEN);
    int lvl, ptInd;
    for(lvl = 1; lvl < NUM_OF_LEVELS; lvl++){
        ptInd = getPtInd(vpn, lvl);
        if(!isValid(ptPtr[ptInd])){
            ptPtr[ptInd] = (alloc_page_frame() << OFFSET_LEN);
            setValid(ptPtr, ptInd);
        }
        ptPtr = (uint64_t*)phys_to_virt(ptPtr[ptInd] & ZERO_VALUE_OFFSET_MASK);
    }
    ptInd = getPtInd(vpn, lvl);
    if(ppn == NO_MAPPING){
        if(isValid(ptPtr[ptInd]))
            setNotValid(ptPtr, ptInd);
    }
    else{
        ptPtr[ptInd] = (ppn << OFFSET_LEN);
        if(!isValid(ptPtr[ptInd]))
            setValid(ptPtr, ptInd);
    }
}

/**
 * A function to query the mapping of a VPN in a PT
 * @param pt - the PPN of the PT root (can assume that it was returned by alloc_page_frame
 * @param vpn - the VPN the caller wishes to find it's mapping
 * @return - the PPN that vpn is mapped to, or NO_MAPPING if no mapping exist
 */
uint64_t page_table_query(uint64_t pt, uint64_t vpn){
    uint64_t *ptPtr = phys_to_virt(pt << OFFSET_LEN);
    int lvl, ptInd;
    for(lvl = 1; lvl < NUM_OF_LEVELS; lvl++){
        ptInd = getPtInd(vpn, lvl);
        if(isValid(ptPtr[ptInd]))
            ptPtr = (uint64_t*)phys_to_virt(ptPtr[ptInd] & ZERO_VALUE_OFFSET_MASK);
        else
            return NO_MAPPING;
    }
    uint64_t pte = ptPtr[getPtInd(vpn, lvl)];
    return isValid(pte) ? (pte >> OFFSET_LEN) : NO_MAPPING;
}
