/*
 * Copyright (c) 2015-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "gm20b/hal_gm20b.h"
#include "common/linux/vgpu/vgpu.h"
#include "common/linux/vgpu/fifo_vgpu.h"
#include "common/linux/vgpu/gr_vgpu.h"
#include "common/linux/vgpu/ltc_vgpu.h"
#include "common/linux/vgpu/mm_vgpu.h"
#include "common/linux/vgpu/dbg_vgpu.h"
#include "common/linux/vgpu/fecs_trace_vgpu.h"
#include "common/linux/vgpu/css_vgpu.h"
#include "vgpu_gr_gm20b.h"
#include "vgpu_fuse_gm20b.h"

#include "gk20a/bus_gk20a.h"
#include "gk20a/flcn_gk20a.h"
#include "gk20a/mc_gk20a.h"
#include "gk20a/fb_gk20a.h"

#include "gm20b/gr_gm20b.h"
#include "gm20b/fifo_gm20b.h"
#include "gm20b/acr_gm20b.h"
#include "gm20b/pmu_gm20b.h"
#include "gm20b/fb_gm20b.h"
#include "gm20b/bus_gm20b.h"
#include "gm20b/regops_gm20b.h"
#include "gm20b/clk_gm20b.h"
#include "gm20b/therm_gm20b.h"
#include "gm20b/mm_gm20b.h"
#include "gm20b/gr_ctx_gm20b.h"
#include "gm20b/gm20b_gating_reglist.h"
#include "gm20b/ltc_gm20b.h"

#include <nvgpu/enabled.h>

#include <nvgpu/hw/gm20b/hw_fuse_gm20b.h>
#include <nvgpu/hw/gm20b/hw_pwr_gm20b.h>
#include <nvgpu/hw/gm20b/hw_fifo_gm20b.h>
#include <nvgpu/hw/gm20b/hw_ram_gm20b.h>

static const struct gpu_ops vgpu_gm20b_ops = {
	.ltc = {
		.determine_L2_size_bytes = vgpu_determine_L2_size_bytes,
		.set_zbc_color_entry = gm20b_ltc_set_zbc_color_entry,
		.set_zbc_depth_entry = gm20b_ltc_set_zbc_depth_entry,
		.init_cbc = gm20b_ltc_init_cbc,
		.init_fs_state = vgpu_ltc_init_fs_state,
		.init_comptags = vgpu_ltc_init_comptags,
		.cbc_ctrl = NULL,
		.isr = gm20b_ltc_isr,
		.cbc_fix_config = gm20b_ltc_cbc_fix_config,
		.flush = gm20b_flush_ltc,
		.set_enabled = gm20b_ltc_set_enabled,
	},
	.ce2 = {
		.isr_stall = gk20a_ce2_isr,
		.isr_nonstall = gk20a_ce2_nonstall_isr,
		.get_num_pce = vgpu_ce_get_num_pce,
	},
	.gr = {
		.get_patch_slots = gr_gk20a_get_patch_slots,
		.init_gpc_mmu = gr_gm20b_init_gpc_mmu,
		.bundle_cb_defaults = gr_gm20b_bundle_cb_defaults,
		.cb_size_default = gr_gm20b_cb_size_default,
		.calc_global_ctx_buffer_size =
			gr_gm20b_calc_global_ctx_buffer_size,
		.commit_global_attrib_cb = gr_gm20b_commit_global_attrib_cb,
		.commit_global_bundle_cb = gr_gm20b_commit_global_bundle_cb,
		.commit_global_cb_manager = gr_gm20b_commit_global_cb_manager,
		.commit_global_pagepool = gr_gm20b_commit_global_pagepool,
		.handle_sw_method = gr_gm20b_handle_sw_method,
		.set_alpha_circular_buffer_size =
			gr_gm20b_set_alpha_circular_buffer_size,
		.set_circular_buffer_size = gr_gm20b_set_circular_buffer_size,
		.enable_hww_exceptions = gr_gk20a_enable_hww_exceptions,
		.is_valid_class = gr_gm20b_is_valid_class,
		.is_valid_gfx_class = gr_gm20b_is_valid_gfx_class,
		.is_valid_compute_class = gr_gm20b_is_valid_compute_class,
		.get_sm_dsm_perf_regs = gr_gm20b_get_sm_dsm_perf_regs,
		.get_sm_dsm_perf_ctrl_regs = gr_gm20b_get_sm_dsm_perf_ctrl_regs,
		.init_fs_state = vgpu_gm20b_init_fs_state,
		.set_hww_esr_report_mask = gr_gm20b_set_hww_esr_report_mask,
		.falcon_load_ucode = gr_gm20b_load_ctxsw_ucode_segments,
		.load_ctxsw_ucode = gr_gk20a_load_ctxsw_ucode,
		.set_gpc_tpc_mask = gr_gm20b_set_gpc_tpc_mask,
		.get_gpc_tpc_mask = vgpu_gr_get_gpc_tpc_mask,
		.free_channel_ctx = vgpu_gr_free_channel_ctx,
		.alloc_obj_ctx = vgpu_gr_alloc_obj_ctx,
		.bind_ctxsw_zcull = vgpu_gr_bind_ctxsw_zcull,
		.get_zcull_info = vgpu_gr_get_zcull_info,
		.is_tpc_addr = gr_gm20b_is_tpc_addr,
		.get_tpc_num = gr_gm20b_get_tpc_num,
		.detect_sm_arch = vgpu_gr_detect_sm_arch,
		.add_zbc_color = gr_gk20a_add_zbc_color,
		.add_zbc_depth = gr_gk20a_add_zbc_depth,
		.zbc_set_table = vgpu_gr_add_zbc,
		.zbc_query_table = vgpu_gr_query_zbc,
		.pmu_save_zbc = gk20a_pmu_save_zbc,
		.add_zbc = gr_gk20a_add_zbc,
		.pagepool_default_size = gr_gm20b_pagepool_default_size,
		.init_ctx_state = vgpu_gr_init_ctx_state,
		.alloc_gr_ctx = vgpu_gr_alloc_gr_ctx,
		.free_gr_ctx = vgpu_gr_free_gr_ctx,
		.update_ctxsw_preemption_mode =
			gr_gm20b_update_ctxsw_preemption_mode,
		.dump_gr_regs = NULL,
		.update_pc_sampling = gr_gm20b_update_pc_sampling,
		.get_fbp_en_mask = vgpu_gr_get_fbp_en_mask,
		.get_max_ltc_per_fbp = vgpu_gr_get_max_ltc_per_fbp,
		.get_max_lts_per_ltc = vgpu_gr_get_max_lts_per_ltc,
		.get_rop_l2_en_mask = vgpu_gr_rop_l2_en_mask,
		.get_max_fbps_count = vgpu_gr_get_max_fbps_count,
		.init_sm_dsm_reg_info = gr_gm20b_init_sm_dsm_reg_info,
		.wait_empty = gr_gk20a_wait_idle,
		.init_cyclestats = vgpu_gr_gm20b_init_cyclestats,
		.set_sm_debug_mode = vgpu_gr_set_sm_debug_mode,
		.enable_cde_in_fecs = gr_gm20b_enable_cde_in_fecs,
		.bpt_reg_info = gr_gm20b_bpt_reg_info,
		.get_access_map = gr_gm20b_get_access_map,
		.handle_fecs_error = gk20a_gr_handle_fecs_error,
		.handle_sm_exception = gr_gk20a_handle_sm_exception,
		.handle_tex_exception = gr_gk20a_handle_tex_exception,
		.enable_gpc_exceptions = gk20a_gr_enable_gpc_exceptions,
		.enable_exceptions = gk20a_gr_enable_exceptions,
		.get_lrf_tex_ltc_dram_override = NULL,
		.update_smpc_ctxsw_mode = vgpu_gr_update_smpc_ctxsw_mode,
		.update_hwpm_ctxsw_mode = vgpu_gr_update_hwpm_ctxsw_mode,
		.record_sm_error_state = gm20b_gr_record_sm_error_state,
		.update_sm_error_state = gm20b_gr_update_sm_error_state,
		.clear_sm_error_state = vgpu_gr_clear_sm_error_state,
		.suspend_contexts = vgpu_gr_suspend_contexts,
		.resume_contexts = vgpu_gr_resume_contexts,
		.get_preemption_mode_flags = gr_gm20b_get_preemption_mode_flags,
		.init_sm_id_table = gr_gk20a_init_sm_id_table,
		.load_smid_config = gr_gm20b_load_smid_config,
		.program_sm_id_numbering = gr_gm20b_program_sm_id_numbering,
		.is_ltcs_ltss_addr = gr_gm20b_is_ltcs_ltss_addr,
		.is_ltcn_ltss_addr = gr_gm20b_is_ltcn_ltss_addr,
		.split_lts_broadcast_addr = gr_gm20b_split_lts_broadcast_addr,
		.split_ltc_broadcast_addr = gr_gm20b_split_ltc_broadcast_addr,
		.setup_rop_mapping = gr_gk20a_setup_rop_mapping,
		.program_zcull_mapping = gr_gk20a_program_zcull_mapping,
		.commit_global_timeslice = gr_gk20a_commit_global_timeslice,
		.commit_inst = vgpu_gr_commit_inst,
		.write_zcull_ptr = gr_gk20a_write_zcull_ptr,
		.write_pm_ptr = gr_gk20a_write_pm_ptr,
		.init_elcg_mode = gr_gk20a_init_elcg_mode,
		.load_tpc_mask = gr_gm20b_load_tpc_mask,
		.inval_icache = gr_gk20a_inval_icache,
		.trigger_suspend = gr_gk20a_trigger_suspend,
		.wait_for_pause = gr_gk20a_wait_for_pause,
		.resume_from_pause = gr_gk20a_resume_from_pause,
		.clear_sm_errors = gr_gk20a_clear_sm_errors,
		.tpc_enabled_exceptions = gr_gk20a_tpc_enabled_exceptions,
		.get_esr_sm_sel = gk20a_gr_get_esr_sm_sel,
		.sm_debugger_attached = gk20a_gr_sm_debugger_attached,
		.suspend_single_sm = gk20a_gr_suspend_single_sm,
		.suspend_all_sms = gk20a_gr_suspend_all_sms,
		.resume_single_sm = gk20a_gr_resume_single_sm,
		.resume_all_sms = gk20a_gr_resume_all_sms,
		.get_sm_hww_warp_esr = gk20a_gr_get_sm_hww_warp_esr,
		.get_sm_hww_global_esr = gk20a_gr_get_sm_hww_global_esr,
		.get_sm_no_lock_down_hww_global_esr_mask =
			gk20a_gr_get_sm_no_lock_down_hww_global_esr_mask,
		.lock_down_sm = gk20a_gr_lock_down_sm,
		.wait_for_sm_lock_down = gk20a_gr_wait_for_sm_lock_down,
		.clear_sm_hww = gm20b_gr_clear_sm_hww,
		.init_ovr_sm_dsm_perf =  gk20a_gr_init_ovr_sm_dsm_perf,
		.get_ovr_perf_regs = gk20a_gr_get_ovr_perf_regs,
		.disable_rd_coalesce = gm20a_gr_disable_rd_coalesce,
		.init_ctxsw_hdr_data = gk20a_gr_init_ctxsw_hdr_data,
		.set_boosted_ctx = NULL,
		.update_boosted_ctx = NULL,
	},
	.fb = {
		.reset = fb_gk20a_reset,
		.init_hw = gk20a_fb_init_hw,
		.init_fs_state = fb_gm20b_init_fs_state,
		.set_mmu_page_size = gm20b_fb_set_mmu_page_size,
		.set_use_full_comp_tag_line =
			gm20b_fb_set_use_full_comp_tag_line,
		.compression_page_size = gm20b_fb_compression_page_size,
		.compressible_page_size = gm20b_fb_compressible_page_size,
		.compression_align_mask = gm20b_fb_compression_align_mask,
		.vpr_info_fetch = gm20b_fb_vpr_info_fetch,
		.dump_vpr_wpr_info = gm20b_fb_dump_vpr_wpr_info,
		.read_wpr_info = gm20b_fb_read_wpr_info,
		.is_debug_mode_enabled = NULL,
		.set_debug_mode = vgpu_mm_mmu_set_debug_mode,
		.tlb_invalidate = vgpu_mm_tlb_invalidate,
	},
	.clock_gating = {
		.slcg_bus_load_gating_prod =
			gm20b_slcg_bus_load_gating_prod,
		.slcg_ce2_load_gating_prod =
			gm20b_slcg_ce2_load_gating_prod,
		.slcg_chiplet_load_gating_prod =
			gm20b_slcg_chiplet_load_gating_prod,
		.slcg_ctxsw_firmware_load_gating_prod =
			gm20b_slcg_ctxsw_firmware_load_gating_prod,
		.slcg_fb_load_gating_prod =
			gm20b_slcg_fb_load_gating_prod,
		.slcg_fifo_load_gating_prod =
			gm20b_slcg_fifo_load_gating_prod,
		.slcg_gr_load_gating_prod =
			gr_gm20b_slcg_gr_load_gating_prod,
		.slcg_ltc_load_gating_prod =
			ltc_gm20b_slcg_ltc_load_gating_prod,
		.slcg_perf_load_gating_prod =
			gm20b_slcg_perf_load_gating_prod,
		.slcg_priring_load_gating_prod =
			gm20b_slcg_priring_load_gating_prod,
		.slcg_pmu_load_gating_prod =
			gm20b_slcg_pmu_load_gating_prod,
		.slcg_therm_load_gating_prod =
			gm20b_slcg_therm_load_gating_prod,
		.slcg_xbar_load_gating_prod =
			gm20b_slcg_xbar_load_gating_prod,
		.blcg_bus_load_gating_prod =
			gm20b_blcg_bus_load_gating_prod,
		.blcg_ctxsw_firmware_load_gating_prod =
			gm20b_blcg_ctxsw_firmware_load_gating_prod,
		.blcg_fb_load_gating_prod =
			gm20b_blcg_fb_load_gating_prod,
		.blcg_fifo_load_gating_prod =
			gm20b_blcg_fifo_load_gating_prod,
		.blcg_gr_load_gating_prod =
			gm20b_blcg_gr_load_gating_prod,
		.blcg_ltc_load_gating_prod =
			gm20b_blcg_ltc_load_gating_prod,
		.blcg_pwr_csb_load_gating_prod =
			gm20b_blcg_pwr_csb_load_gating_prod,
		.blcg_xbar_load_gating_prod =
			gm20b_blcg_xbar_load_gating_prod,
		.blcg_pmu_load_gating_prod =
			gm20b_blcg_pmu_load_gating_prod,
		.pg_gr_load_gating_prod =
			gr_gm20b_pg_gr_load_gating_prod,
	},
	.fifo = {
		.init_fifo_setup_hw = vgpu_init_fifo_setup_hw,
		.bind_channel = vgpu_channel_bind,
		.unbind_channel = vgpu_channel_unbind,
		.disable_channel = vgpu_channel_disable,
		.enable_channel = vgpu_channel_enable,
		.alloc_inst = vgpu_channel_alloc_inst,
		.free_inst = vgpu_channel_free_inst,
		.setup_ramfc = vgpu_channel_setup_ramfc,
		.default_timeslice_us = vgpu_fifo_default_timeslice_us,
		.setup_userd = gk20a_fifo_setup_userd,
		.userd_gp_get = gk20a_fifo_userd_gp_get,
		.userd_gp_put = gk20a_fifo_userd_gp_put,
		.userd_pb_get = gk20a_fifo_userd_pb_get,
		.pbdma_acquire_val = gk20a_fifo_pbdma_acquire_val,
		.preempt_channel = vgpu_fifo_preempt_channel,
		.preempt_tsg = vgpu_fifo_preempt_tsg,
		.enable_tsg = vgpu_enable_tsg,
		.disable_tsg = gk20a_disable_tsg,
		.tsg_verify_channel_status = NULL,
		.tsg_verify_status_ctx_reload = NULL,
		.update_runlist = vgpu_fifo_update_runlist,
		.trigger_mmu_fault = gm20b_fifo_trigger_mmu_fault,
		.get_mmu_fault_info = gk20a_fifo_get_mmu_fault_info,
		.wait_engine_idle = vgpu_fifo_wait_engine_idle,
		.get_num_fifos = gm20b_fifo_get_num_fifos,
		.get_pbdma_signature = gk20a_fifo_get_pbdma_signature,
		.set_runlist_interleave = vgpu_fifo_set_runlist_interleave,
		.tsg_set_timeslice = vgpu_tsg_set_timeslice,
		.tsg_open = vgpu_tsg_open,
		.force_reset_ch = vgpu_fifo_force_reset_ch,
		.engine_enum_from_type = gk20a_fifo_engine_enum_from_type,
		.device_info_data_parse = gm20b_device_info_data_parse,
		.eng_runlist_base_size = fifo_eng_runlist_base__size_1_v,
		.init_engine_info = vgpu_fifo_init_engine_info,
		.runlist_entry_size = ram_rl_entry_size_v,
		.get_tsg_runlist_entry = gk20a_get_tsg_runlist_entry,
		.get_ch_runlist_entry = gk20a_get_ch_runlist_entry,
		.is_fault_engine_subid_gpc = gk20a_is_fault_engine_subid_gpc,
		.dump_pbdma_status = gk20a_dump_pbdma_status,
		.dump_eng_status = gk20a_dump_eng_status,
		.dump_channel_status_ramfc = gk20a_dump_channel_status_ramfc,
		.intr_0_error_mask = gk20a_fifo_intr_0_error_mask,
		.is_preempt_pending = gk20a_fifo_is_preempt_pending,
		.init_pbdma_intr_descs = gm20b_fifo_init_pbdma_intr_descs,
		.reset_enable_hw = gk20a_init_fifo_reset_enable_hw,
		.teardown_ch_tsg = gk20a_fifo_teardown_ch_tsg,
		.handle_sched_error = gk20a_fifo_handle_sched_error,
		.handle_pbdma_intr_0 = gk20a_fifo_handle_pbdma_intr_0,
		.handle_pbdma_intr_1 = gk20a_fifo_handle_pbdma_intr_1,
		.tsg_bind_channel = vgpu_tsg_bind_channel,
		.tsg_unbind_channel = vgpu_tsg_unbind_channel,
#ifdef CONFIG_TEGRA_GK20A_NVHOST
		.alloc_syncpt_buf = gk20a_fifo_alloc_syncpt_buf,
		.free_syncpt_buf = gk20a_fifo_free_syncpt_buf,
		.add_syncpt_wait_cmd = gk20a_fifo_add_syncpt_wait_cmd,
		.get_syncpt_wait_cmd_size = gk20a_fifo_get_syncpt_wait_cmd_size,
		.add_syncpt_incr_cmd = gk20a_fifo_add_syncpt_incr_cmd,
		.get_syncpt_incr_cmd_size = gk20a_fifo_get_syncpt_incr_cmd_size,
#endif
	},
	.gr_ctx = {
		.get_netlist_name = gr_gm20b_get_netlist_name,
		.is_fw_defined = gr_gm20b_is_firmware_defined,
	},
	.mm = {
		.support_sparse = gm20b_mm_support_sparse,
		.gmmu_map = vgpu_locked_gmmu_map,
		.gmmu_unmap = vgpu_locked_gmmu_unmap,
		.vm_bind_channel = vgpu_vm_bind_channel,
		.fb_flush = vgpu_mm_fb_flush,
		.l2_invalidate = vgpu_mm_l2_invalidate,
		.l2_flush = vgpu_mm_l2_flush,
		.cbc_clean = gk20a_mm_cbc_clean,
		.set_big_page_size = gm20b_mm_set_big_page_size,
		.get_big_page_sizes = gm20b_mm_get_big_page_sizes,
		.get_default_big_page_size = gm20b_mm_get_default_big_page_size,
		.gpu_phys_addr = gm20b_gpu_phys_addr,
		.get_iommu_bit = gk20a_mm_get_iommu_bit,
		.get_mmu_levels = gk20a_mm_get_mmu_levels,
		.init_pdb = gk20a_mm_init_pdb,
		.init_mm_setup_hw = NULL,
		.is_bar1_supported = gm20b_mm_is_bar1_supported,
		.init_inst_block = gk20a_init_inst_block,
		.mmu_fault_pending = gk20a_fifo_mmu_fault_pending,
		.get_kind_invalid = gm20b_get_kind_invalid,
		.get_kind_pitch = gm20b_get_kind_pitch,
	},
	.therm = {
		.init_therm_setup_hw = gm20b_init_therm_setup_hw,
		.elcg_init_idle_filters = gk20a_elcg_init_idle_filters,
	},
	.pmu = {
		.pmu_setup_elpg = gm20b_pmu_setup_elpg,
		.pmu_get_queue_head = pwr_pmu_queue_head_r,
		.pmu_get_queue_head_size = pwr_pmu_queue_head__size_1_v,
		.pmu_get_queue_tail = pwr_pmu_queue_tail_r,
		.pmu_get_queue_tail_size = pwr_pmu_queue_tail__size_1_v,
		.pmu_queue_head = gk20a_pmu_queue_head,
		.pmu_queue_tail = gk20a_pmu_queue_tail,
		.pmu_msgq_tail = gk20a_pmu_msgq_tail,
		.pmu_mutex_size = pwr_pmu_mutex__size_1_v,
		.pmu_mutex_acquire = gk20a_pmu_mutex_acquire,
		.pmu_mutex_release = gk20a_pmu_mutex_release,
		.write_dmatrfbase = gm20b_write_dmatrfbase,
		.pmu_elpg_statistics = gk20a_pmu_elpg_statistics,
		.pmu_pg_init_param = NULL,
		.pmu_pg_supported_engines_list = gk20a_pmu_pg_engines_list,
		.pmu_pg_engines_feature_list = gk20a_pmu_pg_feature_list,
		.pmu_is_lpwr_feature_supported = NULL,
		.pmu_lpwr_enable_pg = NULL,
		.pmu_lpwr_disable_pg = NULL,
		.pmu_pg_param_post_init = NULL,
		.dump_secure_fuses = pmu_dump_security_fuses_gm20b,
		.reset_engine = gk20a_pmu_engine_reset,
		.is_engine_in_reset = gk20a_pmu_is_engine_in_reset,
	},
	.clk = {
		.init_clk_support = gm20b_init_clk_support,
		.suspend_clk_support = gm20b_suspend_clk_support,
#ifdef CONFIG_DEBUG_FS
		.init_debugfs = gm20b_clk_init_debugfs,
#endif
		.get_voltage = gm20b_clk_get_voltage,
		.get_gpcclk_clock_counter = gm20b_clk_get_gpcclk_clock_counter,
		.pll_reg_write = gm20b_clk_pll_reg_write,
		.get_pll_debug_data = gm20b_clk_get_pll_debug_data,
	},
	.regops = {
		.get_global_whitelist_ranges =
			gm20b_get_global_whitelist_ranges,
		.get_global_whitelist_ranges_count =
			gm20b_get_global_whitelist_ranges_count,
		.get_context_whitelist_ranges =
			gm20b_get_context_whitelist_ranges,
		.get_context_whitelist_ranges_count =
			gm20b_get_context_whitelist_ranges_count,
		.get_runcontrol_whitelist = gm20b_get_runcontrol_whitelist,
		.get_runcontrol_whitelist_count =
			gm20b_get_runcontrol_whitelist_count,
		.get_runcontrol_whitelist_ranges =
			gm20b_get_runcontrol_whitelist_ranges,
		.get_runcontrol_whitelist_ranges_count =
			gm20b_get_runcontrol_whitelist_ranges_count,
		.get_qctl_whitelist = gm20b_get_qctl_whitelist,
		.get_qctl_whitelist_count = gm20b_get_qctl_whitelist_count,
		.get_qctl_whitelist_ranges = gm20b_get_qctl_whitelist_ranges,
		.get_qctl_whitelist_ranges_count =
			gm20b_get_qctl_whitelist_ranges_count,
		.apply_smpc_war = gm20b_apply_smpc_war,
	},
	.mc = {
		.intr_enable = mc_gk20a_intr_enable,
		.intr_unit_config = mc_gk20a_intr_unit_config,
		.isr_stall = mc_gk20a_isr_stall,
		.intr_stall = mc_gk20a_intr_stall,
		.intr_stall_pause = mc_gk20a_intr_stall_pause,
		.intr_stall_resume = mc_gk20a_intr_stall_resume,
		.intr_nonstall = mc_gk20a_intr_nonstall,
		.intr_nonstall_pause = mc_gk20a_intr_nonstall_pause,
		.intr_nonstall_resume = mc_gk20a_intr_nonstall_resume,
		.enable = gk20a_mc_enable,
		.disable = gk20a_mc_disable,
		.reset = gk20a_mc_reset,
		.boot_0 = gk20a_mc_boot_0,
		.is_intr1_pending = mc_gk20a_is_intr1_pending,
	},
	.debug = {
		.show_dump = NULL,
	},
	.dbg_session_ops = {
		.exec_reg_ops = vgpu_exec_regops,
		.dbg_set_powergate = vgpu_dbg_set_powergate,
		.check_and_set_global_reservation =
			vgpu_check_and_set_global_reservation,
		.check_and_set_context_reservation =
			vgpu_check_and_set_context_reservation,
		.release_profiler_reservation =
			vgpu_release_profiler_reservation,
		.perfbuffer_enable = vgpu_perfbuffer_enable,
		.perfbuffer_disable = vgpu_perfbuffer_disable,
	},
	.bus = {
		.init_hw = gk20a_bus_init_hw,
		.isr = gk20a_bus_isr,
		.read_ptimer = vgpu_read_ptimer,
		.get_timestamps_zipper = vgpu_get_timestamps_zipper,
		.bar1_bind = gm20b_bus_bar1_bind,
	},
#if defined(CONFIG_GK20A_CYCLE_STATS)
	.css = {
		.enable_snapshot = vgpu_css_enable_snapshot_buffer,
		.disable_snapshot = vgpu_css_release_snapshot_buffer,
		.check_data_available = vgpu_css_flush_snapshots,
		.detach_snapshot = vgpu_css_detach,
		.set_handled_snapshots = NULL,
		.allocate_perfmon_ids = NULL,
		.release_perfmon_ids = NULL,
	},
#endif
	.falcon = {
		.falcon_hal_sw_init = gk20a_falcon_hal_sw_init,
	},
	.priv_ring = {
		.isr = gk20a_priv_ring_isr,
	},
	.fuse = {
		.check_priv_security = vgpu_gm20b_fuse_check_priv_security,
	},
	.chip_init_gpu_characteristics = vgpu_init_gpu_characteristics,
	.get_litter_value = gm20b_get_litter_value,
};

int vgpu_gm20b_init_hal(struct gk20a *g)
{
	struct gpu_ops *gops = &g->ops;

	gops->ltc = vgpu_gm20b_ops.ltc;
	gops->ce2 = vgpu_gm20b_ops.ce2;
	gops->gr = vgpu_gm20b_ops.gr;
	gops->fb = vgpu_gm20b_ops.fb;
	gops->clock_gating = vgpu_gm20b_ops.clock_gating;
	gops->fifo = vgpu_gm20b_ops.fifo;
	gops->gr_ctx = vgpu_gm20b_ops.gr_ctx;
	gops->mm = vgpu_gm20b_ops.mm;
	gops->therm = vgpu_gm20b_ops.therm;
	gops->pmu = vgpu_gm20b_ops.pmu;
	/*
	 * clk must be assigned member by member
	 * since some clk ops are assigned during probe prior to HAL init
	 */
	gops->clk.init_clk_support = vgpu_gm20b_ops.clk.init_clk_support;
	gops->clk.suspend_clk_support = vgpu_gm20b_ops.clk.suspend_clk_support;
	gops->clk.get_voltage = vgpu_gm20b_ops.clk.get_voltage;
	gops->clk.get_gpcclk_clock_counter =
		vgpu_gm20b_ops.clk.get_gpcclk_clock_counter;
	gops->clk.pll_reg_write = vgpu_gm20b_ops.clk.pll_reg_write;
	gops->clk.get_pll_debug_data = vgpu_gm20b_ops.clk.get_pll_debug_data;

	gops->regops = vgpu_gm20b_ops.regops;
	gops->mc = vgpu_gm20b_ops.mc;
	gops->dbg_session_ops = vgpu_gm20b_ops.dbg_session_ops;
	gops->debug = vgpu_gm20b_ops.debug;
	gops->bus = vgpu_gm20b_ops.bus;
#if defined(CONFIG_GK20A_CYCLE_STATS)
	gops->css = vgpu_gm20b_ops.css;
#endif
	gops->falcon = vgpu_gm20b_ops.falcon;

	gops->priv_ring = vgpu_gm20b_ops.priv_ring;

	gops->fuse = vgpu_gm20b_ops.fuse;

	/* Lone functions */
	gops->chip_init_gpu_characteristics =
		vgpu_gm20b_ops.chip_init_gpu_characteristics;
	gops->get_litter_value = vgpu_gm20b_ops.get_litter_value;

	__nvgpu_set_enabled(g, NVGPU_GR_USE_DMA_FOR_FW_BOOTSTRAP, true);
	__nvgpu_set_enabled(g, NVGPU_PMU_PSTATE, false);

	/* Read fuses to check if gpu needs to boot in secure/non-secure mode */
	if (gops->fuse.check_priv_security(g))
		return -EINVAL; /* Do not boot gpu */

	/* priv security dependent ops */
	if (nvgpu_is_enabled(g, NVGPU_SEC_PRIVSECURITY)) {
		/* Add in ops from gm20b acr */
		gops->pmu.is_pmu_supported = gm20b_is_pmu_supported;
		gops->pmu.prepare_ucode = prepare_ucode_blob;
		gops->pmu.pmu_setup_hw_and_bootstrap = gm20b_bootstrap_hs_flcn;
		gops->pmu.is_lazy_bootstrap = gm20b_is_lazy_bootstrap;
		gops->pmu.is_priv_load = gm20b_is_priv_load;
		gops->pmu.get_wpr = gm20b_wpr_info;
		gops->pmu.alloc_blob_space = gm20b_alloc_blob_space;
		gops->pmu.pmu_populate_loader_cfg =
			gm20b_pmu_populate_loader_cfg;
		gops->pmu.flcn_populate_bl_dmem_desc =
			gm20b_flcn_populate_bl_dmem_desc;
		gops->pmu.falcon_wait_for_halt = pmu_wait_for_halt;
		gops->pmu.falcon_clear_halt_interrupt_status =
			clear_halt_interrupt_status;
		gops->pmu.init_falcon_setup_hw = gm20b_init_pmu_setup_hw1;

		gops->pmu.init_wpr_region = gm20b_pmu_init_acr;
		gops->pmu.load_lsfalcon_ucode = gm20b_load_falcon_ucode;

		gops->gr.load_ctxsw_ucode = gr_gm20b_load_ctxsw_ucode;
	} else {
		/* Inherit from gk20a */
		gops->pmu.is_pmu_supported = gk20a_is_pmu_supported;
		gops->pmu.prepare_ucode = nvgpu_pmu_prepare_ns_ucode_blob;
		gops->pmu.pmu_setup_hw_and_bootstrap = gk20a_init_pmu_setup_hw1;
		gops->pmu.pmu_nsbootstrap = pmu_bootstrap;

		gops->pmu.load_lsfalcon_ucode = NULL;
		gops->pmu.init_wpr_region = NULL;

		gops->gr.load_ctxsw_ucode = gr_gk20a_load_ctxsw_ucode;
	}

	__nvgpu_set_enabled(g, NVGPU_PMU_FECS_BOOTSTRAP_DONE, false);
	g->pmu_lsf_pmu_wpr_init_done = 0;
	g->bootstrap_owner = LSF_BOOTSTRAP_OWNER_DEFAULT;

	g->name = "gm20b";

	return 0;
}
