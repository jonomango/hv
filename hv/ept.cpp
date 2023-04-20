#include "ept.h"
#include "arch.h"
#include "vcpu.h"
#include "mtrr.h"
#include "mm.h"

namespace hv {

// identity-map the EPT paging structures
void prepare_ept(vcpu_ept_data& ept) {
  memset(&ept, 0, sizeof(ept));

  ept.dummy_page_pfn = MmGetPhysicalAddress(ept.dummy_page).QuadPart >> 12;

  ept.num_used_free_pages = 0;

  for (size_t i = 0; i < ept_free_page_count; ++i)
    ept.free_page_pfns[i] = MmGetPhysicalAddress(&ept.free_pages[i]).QuadPart >> 12;

  ept.hooks.active_list_head = nullptr;
  ept.hooks.free_list_head   = &ept.hooks.buffer[0];

  for (size_t i = 0; i < ept.hooks.capacity - 1; ++i)
    ept.hooks.buffer[i].next = &ept.hooks.buffer[i + 1];

  // the last node points to NULL
  ept.hooks.buffer[ept.hooks.capacity - 1].next = nullptr;

  // setup the first PML4E so that it points to our PDPT
  auto& pml4e             = ept.pml4[0];
  pml4e.flags             = 0;
  pml4e.read_access       = 1;
  pml4e.write_access      = 1;
  pml4e.execute_access    = 1;
  pml4e.accessed          = 0;
  pml4e.user_mode_execute = 1;
  pml4e.page_frame_number = MmGetPhysicalAddress(&ept.pdpt).QuadPart >> 12;

  // MTRR data for setting memory types
  auto const mtrrs = read_mtrr_data();

  // TODO: allocate a PT for the fixed MTRRs region so that we can get
  // more accurate memory typing in that area (as opposed to just
  // mapping the whole PDE as UC).

  for (size_t i = 0; i < ept_pd_count; ++i) {
    // point each PDPTE to the corresponding PD
    auto& pdpte             = ept.pdpt[i];
    pdpte.flags             = 0;
    pdpte.read_access       = 1;
    pdpte.write_access      = 1;
    pdpte.execute_access    = 1;
    pdpte.accessed          = 0;
    pdpte.user_mode_execute = 1;
    pdpte.page_frame_number = MmGetPhysicalAddress(&ept.pds[i]).QuadPart >> 12;

    for (size_t j = 0; j < 512; ++j) {
      // identity-map every GPA to the corresponding HPA
      auto& pde             = ept.pds_2mb[i][j];
      pde.flags             = 0;
      pde.read_access       = 1;
      pde.write_access      = 1;
      pde.execute_access    = 1;
      pde.ignore_pat        = 0;
      pde.large_page        = 1;
      pde.accessed          = 0;
      pde.dirty             = 0;
      pde.user_mode_execute = 1;
      pde.suppress_ve       = 0;
      pde.page_frame_number = (i << 9) + j;
      pde.memory_type       = calc_mtrr_mem_type(mtrrs,
        pde.page_frame_number << 21, 0x1000 << 9);
    }
  }
}

// update the memory types in the EPT paging structures based on the MTRRs.
// this function should only be called from root-mode during vmx-operation.
void update_ept_memory_type(vcpu_ept_data& ept) {
  // TODO: completely virtualize the guest MTRRs
  auto const mtrrs = read_mtrr_data();

  for (size_t i = 0; i < ept_pd_count; ++i) {
    for (size_t j = 0; j < 512; ++j) {
      auto& pde = ept.pds_2mb[i][j];

      // 2MB large page
      if (pde.large_page) {
        // update the memory type for this PDE
        pde.memory_type = calc_mtrr_mem_type(mtrrs,
          pde.page_frame_number << 21, 0x1000 << 9);
      }
      // PDE points to a PT
      else {
        auto const pt = reinterpret_cast<ept_pte*>(host_physical_memory_base
          + (ept.pds[i][j].page_frame_number << 12));

        // update the memory type for every PTE
        for (size_t k = 0; k < 512; ++k) {
          pt[k].memory_type = calc_mtrr_mem_type(mtrrs,
            pt[k].page_frame_number << 12, 0x1000);
        }
      }
    }
  }
}

// set the memory type in every EPT paging structure to the specified value
void set_ept_memory_type(vcpu_ept_data& ept, uint8_t const memory_type) {
  for (size_t i = 0; i < ept_pd_count; ++i) {
    for (size_t j = 0; j < 512; ++j) {
      auto& pde = ept.pds_2mb[i][j];

      // 2MB large page
      if (pde.large_page)
        pde.memory_type = memory_type;
      // PDE points to a PT
      else {
        auto const pt = reinterpret_cast<ept_pte*>(host_physical_memory_base
          + (ept.pds[i][j].page_frame_number << 12));

        // update the memory type for every PTE
        for (size_t k = 0; k < 512; ++k)
          pt[k].memory_type = memory_type;
      }
    }
  }
}

// get the corresponding EPT PDPTE for a given physical address
ept_pdpte* get_ept_pdpte(vcpu_ept_data& ept, uint64_t const physical_address) {
  pml4_virtual_address const addr = { reinterpret_cast<void*>(physical_address) };

  if (addr.pml4_idx != 0)
    return nullptr;

  if (addr.pdpt_idx >= ept_pd_count)
    return nullptr;

  return &ept.pdpt[addr.pdpt_idx];
}

// get the corresponding EPT PDE for a given physical address
ept_pde* get_ept_pde(vcpu_ept_data& ept, uint64_t const physical_address) {
  pml4_virtual_address const addr = { reinterpret_cast<void*>(physical_address) };

  if (addr.pml4_idx != 0)
    return nullptr;

  if (addr.pdpt_idx >= ept_pd_count)
    return nullptr;

  return &ept.pds[addr.pdpt_idx][addr.pd_idx];
}

// get the corresponding EPT PTE for a given physical address
ept_pte* get_ept_pte(vcpu_ept_data& ept,
    uint64_t const physical_address, bool const force_split) {
  pml4_virtual_address const addr = { reinterpret_cast<void*>(physical_address) };

  if (addr.pml4_idx != 0)
    return nullptr;

  if (addr.pdpt_idx >= ept_pd_count)
    return nullptr;

  auto& pde_2mb = ept.pds_2mb[addr.pdpt_idx][addr.pd_idx];

  if (pde_2mb.large_page) {
    if (!force_split)
      return nullptr;

    split_ept_pde(ept, &pde_2mb);

    // failed to split the PDE
    if (pde_2mb.large_page)
      return nullptr;
  }

  auto const pt = reinterpret_cast<ept_pte*>(host_physical_memory_base
    + (ept.pds[addr.pdpt_idx][addr.pd_idx].page_frame_number << 12));

  return &pt[addr.pt_idx];
}

// split a 2MB EPT PDE so that it points to an EPT PT
void split_ept_pde(vcpu_ept_data& ept, ept_pde_2mb* const pde_2mb) {
  // this PDE is already split
  if (!pde_2mb->large_page)
    return;

  // no available free pages
  if (ept.num_used_free_pages >= ept_free_page_count)
    return;

  // allocate a free page for the PT
  auto const pt_pfn = ept.free_page_pfns[ept.num_used_free_pages];
  auto const pt = reinterpret_cast<ept_pte*>(
    &ept.free_pages[ept.num_used_free_pages]);
  ++ept.num_used_free_pages;

  for (size_t i = 0; i < 512; ++i) {
    auto& pte = pt[i];
    pte.flags = 0;

    // copy the parent PDE flags
    pte.read_access             = pde_2mb->read_access;
    pte.write_access            = pde_2mb->write_access;
    pte.execute_access          = pde_2mb->execute_access;
    pte.memory_type             = pde_2mb->memory_type;
    pte.ignore_pat              = pde_2mb->ignore_pat;
    pte.accessed                = pde_2mb->accessed;
    pte.dirty                   = pde_2mb->dirty;
    pte.user_mode_execute       = pde_2mb->user_mode_execute;
    pte.verify_guest_paging     = pde_2mb->verify_guest_paging;
    pte.paging_write_access     = pde_2mb->paging_write_access;
    pte.supervisor_shadow_stack = pde_2mb->supervisor_shadow_stack;
    pte.suppress_ve             = pde_2mb->suppress_ve;
    pte.page_frame_number       = (pde_2mb->page_frame_number << 9) + i;
  }

  auto const pde         = reinterpret_cast<ept_pde*>(pde_2mb);
  pde->flags             = 0;
  pde->read_access       = 1;
  pde->write_access      = 1;
  pde->execute_access    = 1;
  pde->user_mode_execute = 1;
  pde->page_frame_number = pt_pfn;
}

// memory read/written will use the original page while code
// being executed will use the executable page instead
bool install_ept_hook(vcpu_ept_data& ept,
    uint64_t const original_page_pfn,
    uint64_t const executable_page_pfn) {
  // we ran out of EPT hooks :(
  if (!ept.hooks.free_list_head)
    return false;

  // get the EPT PTE, and possible split an existing PDE if needed
  auto const pte = get_ept_pte(ept, original_page_pfn << 12, true);
  if (!pte)
    return false;

  // remove a hook node from the free list
  auto const hook_node = ept.hooks.free_list_head;
  ept.hooks.free_list_head = hook_node->next;

  // insert the hook node into the active list
  hook_node->next = ept.hooks.active_list_head;
  ept.hooks.active_list_head = hook_node;

  // initialize the hook node
  hook_node->orig_pfn = static_cast<uint32_t>(original_page_pfn);
  hook_node->exec_pfn = static_cast<uint32_t>(executable_page_pfn);

  // an instruction fetch to this physical address will now trigger
  // an ept-violation vm-exit where the real "meat" of the ept hook is
  pte->execute_access = 0;

  vmx_invept(invept_all_context, {});

  return true;
}

// remove an EPT hook that was installed with install_ept_hook()
void remove_ept_hook(vcpu_ept_data& ept, uint64_t const original_page_pfn) {
  if (!ept.hooks.active_list_head)
    return;

  // the head is the target node
  if (ept.hooks.active_list_head->orig_pfn == original_page_pfn) {
    auto const new_head = ept.hooks.active_list_head->next;

    // add to the free list
    ept.hooks.active_list_head->next = ept.hooks.free_list_head;
    ept.hooks.free_list_head = ept.hooks.active_list_head;

    // remove from the active list
    ept.hooks.active_list_head = new_head;
  } else {
    auto prev = ept.hooks.active_list_head;

    // search for the node BEFORE the target node (prev if this was doubly)
    while (prev->next) {
      if (prev->next->orig_pfn == original_page_pfn)
        break;

      prev = prev->next;
    }

    if (!prev->next)
      return;

    auto const new_next = prev->next->next;

    // add to the free list
    prev->next->next = ept.hooks.free_list_head;
    ept.hooks.free_list_head = prev->next;

    // remove from the active list
    prev->next = new_next;
  }

  auto const pte = get_ept_pte(ept, original_page_pfn << 12, false);

  // this should NOT fail
  if (!pte)
    return;

  // restore original EPT page attributes
  pte->read_access       = 1;
  pte->write_access      = 1;
  pte->execute_access    = 1;
  pte->page_frame_number = original_page_pfn;

  vmx_invept(invept_all_context, {});
}

// find the EPT hook for the specified PFN
vcpu_ept_hook_node* find_ept_hook(vcpu_ept_data& ept,
    uint64_t const original_page_pfn) {
  // TODO:
  //   maybe use a more optimal data structure to handle a large
  //   amount of EPT hooks?

  // linear search through the active hook list
  for (auto curr = ept.hooks.active_list_head; curr; curr = curr->next) {
    if (curr->orig_pfn == original_page_pfn)
      return curr;
  }

  return nullptr;
}

} // namespace hv

