set(files
  adxintrin.h
  altivec.h
  ammintrin.h
  amxintrin.h
  arm_acle.h
  arm_cmse.h
  armintr.h
  arm64intr.h
  avx2intrin.h
  avx512bf16intrin.h
  avx512bwintrin.h
  avx512bitalgintrin.h
  avx512vlbitalgintrin.h
  avx512cdintrin.h
  avx512vpopcntdqintrin.h
  avx512dqintrin.h
  avx512erintrin.h
  avx512fintrin.h
  avx512fp16intrin.h
  avx512ifmaintrin.h
  avx512ifmavlintrin.h
  avx512pfintrin.h
  avx512vbmiintrin.h
  avx512vbmivlintrin.h
  avx512vbmi2intrin.h
  avx512vlvbmi2intrin.h
  avx512vlbf16intrin.h
  avx512vlbwintrin.h
  avx512vlcdintrin.h
  avx512vldqintrin.h
  avx512vlfp16intrin.h
  avx512vlintrin.h
  avx512vp2intersectintrin.h
  avx512vlvp2intersectintrin.h
  avx512vpopcntdqvlintrin.h
  avx512vnniintrin.h
  avx512vlvnniintrin.h
  avxintrin.h
  avxvnniintrin.h
  bmi2intrin.h
  bmiintrin.h
  builtins.h
  __clang_cuda_builtin_vars.h
  __clang_cuda_math.h
  __clang_cuda_cmath.h
  __clang_cuda_complex_builtins.h
  __clang_cuda_device_functions.h
  __clang_cuda_intrinsics.h
  __clang_cuda_texture_intrinsics.h
  __clang_cuda_libdevice_declares.h
  __clang_cuda_math_forward_declares.h
  __clang_cuda_runtime_wrapper.h
  __clang_hip_libdevice_declares.h
  __clang_hip_cmath.h
  __clang_hip_math.h
  __clang_hip_runtime_wrapper.h
  cetintrin.h
  cet.h
  cldemoteintrin.h
  clzerointrin.h
  crc32intrin.h
  cpuid.h
  clflushoptintrin.h
  clwbintrin.h
  emmintrin.h
  enqcmdintrin.h
  f16cintrin.h
  float.h
  fma4intrin.h
  fmaintrin.h
  fxsrintrin.h
  gfniintrin.h
  hexagon_circ_brev_intrinsics.h
  hexagon_protos.h
  hexagon_types.h
  hvx_hexagon_protos.h
  hresetintrin.h
  htmintrin.h
  htmxlintrin.h
  ia32intrin.h
  immintrin.h
  intrin.h
  inttypes.h
  invpcidintrin.h
  iso646.h
  keylockerintrin.h
  limits.h
  lwpintrin.h
  lzcntintrin.h
  mm3dnow.h
  mmintrin.h
  mm_malloc.h
  module.modulemap
  movdirintrin.h
  msa.h
  mwaitxintrin.h
  nmmintrin.h
  opencl-c.h
  opencl-c-base.h
  pkuintrin.h
  pmmintrin.h
  pconfigintrin.h
  popcntintrin.h
  prfchwintrin.h
  ptrauth.h
  ptwriteintrin.h
  rdseedintrin.h
  rtmintrin.h
  serializeintrin.h
  sgxintrin.h
  s390intrin.h
  shaintrin.h
  smmintrin.h
  stdalign.h
  stdarg.h
  stdatomic.h
  stdbool.h
  stddef.h
  __stddef_max_align_t.h
  stdint.h
  stdnoreturn.h
  tbmintrin.h
  tgmath.h
  tmmintrin.h
  tsxldtrkintrin.h
  uintrintrin.h
  unwind.h
  vadefs.h
  vaesintrin.h
  varargs.h
  vecintrin.h
  vpclmulqdqintrin.h
  waitpkgintrin.h
  wasm_simd128.h
  wbnoinvdintrin.h
  wmmintrin.h
  __wmmintrin_aes.h
  __wmmintrin_pclmul.h
  x86gprintrin.h
  x86intrin.h
  xmmintrin.h
  xopintrin.h
  xsavecintrin.h
  xsaveintrin.h
  xsaveoptintrin.h
  xsavesintrin.h
  xtestintrin.h
  )

set(cuda_wrapper_files
  cuda_wrappers/algorithm
  cuda_wrappers/complex
  cuda_wrappers/new
)

set(ppc_wrapper_files
  ppc_wrappers/mmintrin.h
  ppc_wrappers/xmmintrin.h
  ppc_wrappers/mm_malloc.h
  ppc_wrappers/emmintrin.h
  ppc_wrappers/pmmintrin.h
  ppc_wrappers/tmmintrin.h
  ppc_wrappers/smmintrin.h
)

set(openmp_wrapper_files
  openmp_wrappers/math.h
  openmp_wrappers/cmath
  openmp_wrappers/complex.h
  openmp_wrappers/complex
  openmp_wrappers/__clang_openmp_device_functions.h
  openmp_wrappers/complex_cmath.h
  openmp_wrappers/new
)

set(output_dir ${LLVM_LIBRARY_OUTPUT_INTDIR}/clang/${CLANG_VERSION}/include)
set(out_files)
set(generated_files)

function(copy_header_to_output_dir src_dir file)
  set(src ${src_dir}/${file})
  set(dst ${output_dir}/${file})
  add_custom_command(OUTPUT ${dst}
    DEPENDS ${src}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${src} ${dst}
    COMMENT "Copying clang's ${file}...")
  list(APPEND out_files ${dst})
  set(out_files ${out_files} PARENT_SCOPE)
endfunction(copy_header_to_output_dir)

function(clang_generate_header td_option td_file out_file)
  clang_tablegen(${out_file} ${td_option}
  -I ${CLANG_SOURCE_DIR}/include/clang/Basic/
  SOURCE ${CLANG_SOURCE_DIR}/include/clang/Basic/${td_file})

  copy_header_to_output_dir(${CMAKE_CURRENT_BINARY_DIR} ${out_file})
  set(out_files ${out_files} PARENT_SCOPE)
  list(APPEND generated_files "${CMAKE_CURRENT_BINARY_DIR}/${out_file}")
  set(generated_files ${generated_files} PARENT_SCOPE)
endfunction(clang_generate_header)


# Copy header files from the source directory to the build directory
foreach( f ${files} ${cuda_wrapper_files} ${ppc_wrapper_files} ${openmp_wrapper_files})
  copy_header_to_output_dir(${CMAKE_CURRENT_SOURCE_DIR} ${f})
endforeach( f )

# Generate header files and copy them to the build directory
if(ARM IN_LIST LLVM_TARGETS_TO_BUILD OR AArch64 IN_LIST LLVM_TARGETS_TO_BUILD)
  # Generate arm_neon.h
  clang_generate_header(-gen-arm-neon arm_neon.td arm_neon.h)
  # Generate arm_fp16.h
  clang_generate_header(-gen-arm-fp16 arm_fp16.td arm_fp16.h)
  # Generate arm_sve.h
  clang_generate_header(-gen-arm-sve-header arm_sve.td arm_sve.h)
  # Generate arm_bf16.h
  clang_generate_header(-gen-arm-bf16 arm_bf16.td arm_bf16.h)
  # Generate arm_mve.h
  clang_generate_header(-gen-arm-mve-header arm_mve.td arm_mve.h)
  # Generate arm_cde.h
  clang_generate_header(-gen-arm-cde-header arm_cde.td arm_cde.h)
  # Copy arm_neon_sve_bridge.h
  copy_header_to_output_dir(${CMAKE_CURRENT_SOURCE_DIR}
    arm_neon_sve_bridge.h
  )
endif()
if(RISCV IN_LIST LLVM_TARGETS_TO_BUILD)
  # Generate riscv_vector.h
  clang_generate_header(-gen-riscv-vector-header riscv_vector.td riscv_vector.h)
endif()

add_custom_target(clang-resource-headers ALL DEPENDS ${out_files})
set_target_properties(clang-resource-headers PROPERTIES
  FOLDER "Misc"
  RUNTIME_OUTPUT_DIRECTORY "${output_dir}")

set(header_install_dir lib${LLVM_LIBDIR_SUFFIX}/clang/${CLANG_VERSION}/include)

install(
  FILES ${files} ${generated_files}
  DESTINATION ${header_install_dir}
  COMPONENT clang-resource-headers)

install(
  FILES ${cuda_wrapper_files}
  DESTINATION ${header_install_dir}/cuda_wrappers
  COMPONENT clang-resource-headers)

install(
  FILES ${ppc_wrapper_files}
  DESTINATION ${header_install_dir}/ppc_wrappers
  COMPONENT clang-resource-headers)

install(
  FILES ${openmp_wrapper_files}
  DESTINATION ${header_install_dir}/openmp_wrappers
  COMPONENT clang-resource-headers)

if (NOT LLVM_ENABLE_IDE)
  add_llvm_install_targets(install-clang-resource-headers
                           DEPENDS clang-resource-headers
                           COMPONENT clang-resource-headers)
endif()
