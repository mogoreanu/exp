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

Install git
```
sudo apt install git
```

Clone the repository and validate that a basic binary can be built and run:
```
git clone https://github.com/mogoreanu/exp.git
cd exp
bazel run :my_hello
```

## Useful libraries / binaries

stat/approx_counter - space-efficient and cpu-efficient way to track request / 
byte rate. 