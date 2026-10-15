# LZ77 Compression - Worklet 2

## Overview

This module implements the LZ77 compression stage.

The implementation manually manages:

- Token storage
- Dynamic token array
- Hash table
- Hash chains
- LZ77 matching
- Sliding window constraints

The STL is not used.

## LZ77 Parameters

Search window:

32 KB

Look-ahead buffer:

258 bytes

Maximum hash-chain search depth:

64

## Token Structure

Each LZ77 token contains:

    offset
    length
    nextChar

Example:

    (5, 3, 'D')

means:

- Move 5 positions backward
- Copy 3 characters
- Append 'D'

A literal is represented as:

    (0, 0, character)

## Architecture

    Input
      |
      v
    LZ77 Matcher
      |
      v
    TokenArray
      |
      v
    Worklet 3
      |
      v
    Symbol Creation
      |
      v
    Huffman Coding

## Compilation

Build:

    make

Run:

    ./lz77

Run with an input file:

    ./lz77 input.txt

Build tests:

    make test

Run tests:

    ./test_lz77

Clean:

    make clean