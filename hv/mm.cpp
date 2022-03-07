#include "mm.h"
#include "arch.h"
#include "page-tables.h"
#include "vmx.h"

namespace hv {

// cache MTRR data into a single structure
mtrr_data read_mtrr_data() {
  mtrr_data mtrrs;

  mtrrs.cap.flags      = __readmsr(IA32_MTRR_CAPABILITIES);
  mtrrs.def_type.flags = __readmsr(IA32_MTRR_DEF_TYPE);
  mtrrs.var_count      = 0;

  for (uint32_t i = 0; i < mtrrs.cap.variable_range_count; ++i) {
    ia32_mtrr_physmask_register mask;
    mask.flags = __readmsr(IA32_MTRR_PHYSMASK0 + i * 2);

    if (!mask.valid)
      continue;

    // i seriously doubt this would ever get hit but...
    NT_ASSERT(mtrrs.var_count <= 64);

    mtrrs.variable[mtrrs.var_count].mask = mask;
    mtrrs.variable[mtrrs.var_count].base.flags =
      __readmsr(IA32_MTRR_PHYSBASE0 + i * 2);

    ++mtrrs.var_count;
  }

  return mtrrs;
}

// calculate the MTRR memory type for a single page
static uint8_t calc_mtrr_mem_type(mtrr_data const& mtrrs, uint64_t const pfn) {
  if (!mtrrs.def_type.mtrr_enable)
    return MEMORY_TYPE_UNCACHEABLE;

  // fixed range MTRRs
  if (pfn < 0x100 && mtrrs.cap.fixed_range_supported
      && mtrrs.def_type.fixed_range_mtrr_enable) {
    // TODO: implement this
    return MEMORY_TYPE_UNCACHEABLE;
  }

  uint8_t curr_mem_type = MEMORY_TYPE_INVALID;

  // variable-range MTRRs
  for (uint32_t i = 0; i < mtrrs.var_count; ++i) {
    auto const base = mtrrs.variable[i].base.page_frame_number;
    auto const mask = mtrrs.variable[i].mask.page_frame_number;

    // 3.11.11.2.3
    // essentially checking if the top part of the address (as specified
    // by the PHYSMASK) is equal to the top part of the PHYSBASE.
    if ((pfn & mask) == (base & mask)) {
      auto const type = static_cast<uint8_t>(mtrrs.variable[i].base.type);

      // UC takes precedence over everything
      if (type == MEMORY_TYPE_UNCACHEABLE)
        return MEMORY_TYPE_UNCACHEABLE;

      // this works for WT and WB, which is the only other "defined" overlap scenario
      if (type < curr_mem_type)
        curr_mem_type = type;
    }
  }

  // no MTRR covers the specified address
  if (curr_mem_type == MEMORY_TYPE_INVALID)
    return mtrrs.def_type.default_memory_type;

  return curr_mem_type;
}

// calculate the MTRR memory type for the given physical memory range
uint8_t calc_mtrr_mem_type(mtrr_data const& mtrrs, uint64_t address, uint64_t size) {
  // base address must be on atleast a 4KB boundary
  address &= ~0xFFFull;

  // minimum range size is 4KB
  size = (size + 0xFFF) & ~0xFFFull;

  uint8_t curr_mem_type = MEMORY_TYPE_INVALID;

  for (uint64_t curr = address; curr < address + size; curr += 0x1000) {
    auto const type = calc_mtrr_mem_type(mtrrs, curr >> 12);

    if (type == MEMORY_TYPE_UNCACHEABLE)
      return type;

    // use the worse memory type between the two
    if (type < curr_mem_type)
      curr_mem_type = type;
  }

  NT_ASSERT(curr_mem_type != MEMORY_TYPE_INVALID);

  return curr_mem_type;
}

// translate a GVA to an HVA. offset_to_next_page is the number of bytes to
// the next page (i.e. the number of bytes that can be safely accessed through
// the HVA in order to modify the GVA.
void* gva2hva(cr3 const guest_cr3, void* const guest_virtual_address, size_t* const offset_to_next_page) {
  if (offset_to_next_page)
    *offset_to_next_page = 0;

  pml4_virtual_address const vaddr = { guest_virtual_address };

  // guest PML4
  auto const pml4 = reinterpret_cast<pml4e_64*>(host_physical_memory_base
    + (guest_cr3.address_of_page_directory << 12));
  auto const pml4e = pml4[vaddr.pml4_idx];

  if (!pml4e.present)
    return nullptr;

  // guest PDPT
  auto const pdpt = reinterpret_cast<pdpte_64*>(host_physical_memory_base
    + (pml4e.page_frame_number << 12));
  auto const pdpte = pdpt[vaddr.pdpt_idx];

  if (!pdpte.present)
    return nullptr;

  if (pdpte.large_page) {
    pdpte_1gb_64 pdpte_1gb;
    pdpte_1gb.flags = pdpte.flags;

    auto const offset = (vaddr.pd_idx << 18) + (vaddr.pt_idx << 9) + vaddr.offset;

    // 1GB
    if (offset_to_next_page)
      *offset_to_next_page = 0x40000000 - offset;

    return host_physical_memory_base + (pdpte_1gb.page_frame_number << 30) + offset;
  }

  // guest PD
  auto const pd = reinterpret_cast<pde_64*>(host_physical_memory_base
    + (pdpte.page_frame_number << 12));
  auto const pde = pd[vaddr.pd_idx];

  if (!pde.present)
    return nullptr;

  if (pde.large_page) {
    pde_2mb_64 pde_2mb;
    pde_2mb.flags = pde.flags;

    auto const offset = (vaddr.pt_idx << 9) + vaddr.offset;

    // 2MB page
    if (offset_to_next_page)
      *offset_to_next_page = 0x200000 - offset;

    return host_physical_memory_base + (pde_2mb.page_frame_number << 21) + offset;
  }

  // guest PT
  auto const pt = reinterpret_cast<pte_64*>(host_physical_memory_base
    + (pde.page_frame_number << 12));
  auto const pte = pt[vaddr.pt_idx];

  if (!pte.present)
    return nullptr;

  // 4KB page
  if (offset_to_next_page)
    *offset_to_next_page = 0x1000 - vaddr.offset;

  return host_physical_memory_base + (pte.page_frame_number << 12) + vaddr.offset;
}

// translate a GVA to an HVA. offset_to_next_page is the number of bytes to
// the next page (i.e. the number of bytes that can be safely accessed through
// the HVA in order to modify the GVA.
void* gva2hva(void* const guest_virtual_address, size_t* const offset_to_next_page) {
  cr3 guest_cr3;
  guest_cr3.flags = vmx_vmread(VMCS_GUEST_CR3);
  return gva2hva(guest_cr3, guest_virtual_address, offset_to_next_page);
}

} // namespace hv
