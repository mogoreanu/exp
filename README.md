# exp

The repository contains experimental tools and libraries.

## Environment expectations
* Designed to be built using bazel
* Uses the ABSL libraries
* Optimized for use with vscode

## Getting started

Instructions verified on debian 12

Install bazel using the bazelisk binary, it will provide a more up-to-date 
version that may be required for some of the example invocations.

Install git and a C++ compiler compatible with bazel
```
sudo apt install git g++
```

Clone the repository and validate that a basic binary can be built and run:
```
git clone https://github.com/mogoreanu/exp.git
cd exp
bazel run :my_hello
```

Download and copy buildifier from 
[here](https://github.com/bazelbuild/buildtools/releases) and put it somewhere 
in PATH, for example in `/usr/bin/buildifier`

## vscode setup

Ctrl + P
```
ext install ms-vscode.cpptools-extension-pack
ext install BazelBuild.vscode-bazel
ext install bpfdeploy.bpftrace
```

## Useful libraries / binaries

stat/approx_counter - space-efficient and cpu-efficient way to track request / 
byte rate. 