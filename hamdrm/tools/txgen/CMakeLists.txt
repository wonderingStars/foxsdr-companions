# hamdrm_txgen - stand-alone HamDRM (digital SSTV) test-signal generator, ported from QSSTV's transmit chain.
# GPL (see README.md, CHANGES.md and the file headers). No find_package, no global settings: add_subdirectory() it.
cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED PROJECT_NAME OR PROJECT_NAME STREQUAL "")
  project(hamdrm_txgen LANGUAGES CXX)
endif()

set(_TXG ${CMAKE_CURRENT_LIST_DIR})
set(_TXC ${_TXG}/drmtx/common)

add_executable(hamdrm_txgen
  txgen_main.cpp
  compat/txglobals.cpp
  dsp/downsample_coeffs.cpp
  drmtx/drmtransmitter.cpp
  drmtx/drmparams.cpp
  ${_TXC}/CDrmTransmitter.cpp
  ${_TXC}/DRMSignalIO.cpp
  ${_TXC}/DataIO.cpp
  ${_TXC}/OFDM.cpp
  ${_TXC}/Parameter.cpp
  ${_TXC}/csoundout.cpp
  ${_TXC}/FAC/FAC.cpp
  ${_TXC}/SDC/SDCTransmit.cpp
  ${_TXC}/datadecoding/DABMOT.cpp
  ${_TXC}/datadecoding/DataDecoder.cpp
  ${_TXC}/datadecoding/MOTSlideShow.cpp
  ${_TXC}/interleaver/BlockInterleaver.cpp
  ${_TXC}/interleaver/SymbolInterleaver.cpp
  ${_TXC}/matlib/MatlibSigProToolbox.cpp
  ${_TXC}/matlib/MatlibStdToolbox.cpp
  ${_TXC}/mlc/BitInterleaver.cpp
  ${_TXC}/mlc/ChannelCode.cpp
  ${_TXC}/mlc/ConvEncoder.cpp
  ${_TXC}/mlc/EnergyDispersal.cpp
  ${_TXC}/mlc/MLC.cpp
  ${_TXC}/mlc/QAMMapping.cpp
  ${_TXC}/ofdmcellmapping/CellMappingTable.cpp
  ${_TXC}/ofdmcellmapping/OFDMCellMapping.cpp
  ${_TXC}/sourcedecoders/AudioSourceDecoder.cpp
  ${_TXC}/tables/TableFAC.cpp
  ${_TXC}/util/CRC.cpp
  ${_TXC}/util/Utilities.cpp
)
# compat/ comes first: its appglobal.h, drm.h, supportfunctions.h, fftw3.h stand in for QSSTV's application headers.
target_include_directories(hamdrm_txgen PRIVATE
  ${_TXG}/compat
  ${_TXG}
  ${_TXG}/utils
  ${_TXG}/drmtx
  ${_TXC}
)
target_compile_features(hamdrm_txgen PRIVATE cxx_std_17)
if(MSVC)
  target_compile_definitions(hamdrm_txgen PRIVATE _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES NOMINMAX)
endif()
