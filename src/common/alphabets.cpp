/**
 * @file    alphabets.cpp
 * @ingroup common
 * @author  Patrick Flick
 * @brief   Defines all common alphabets, including DNA, DNA5, RNA, RNA5
 *          AA (IUPAC), DNA_IUPAC, and CUSTOM
 *
 * Copyright (c) TODO
 *
 * TODO add Licence
 */
#include <common/alphabets.hpp>

// TODO properly initialize the translation tables in the .cpp file for correct linkage
constexpr uint8_t DNA::FROM_ASCII[256];
constexpr char DNA::TO_ASCII[DNA::SIZE];
constexpr uint8_t DNA5::FROM_ASCII[256];
constexpr char DNA5::TO_ASCII[DNA5::SIZE];