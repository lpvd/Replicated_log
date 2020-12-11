# Replicated_log

This repository contains implementation of Replicated Log task.

## Prerequisites

Installed boost.

## Usage

After cloning run:

  ```
git submodule update --init
  ```
### To start as a master node:
  ```
replico <mode> <rootaddress> <nodeaddress1;nodeaddress2> <threads>
  ```

### To start as a secondary node:
  ```
replico <mode> <nodeadress> <rootaddress> <threads>
 ```
#### Example
 ```
/* Master */
 replico root 0.0.0.0:8080 0.0.0.0:8081;0.0.0.0:8082 1

/* Secondaries*/
replico node 0.0.0.0:8081 0.0.0.0:8080 1
replico node 0.0.0.0:8082 0.0.0.0:8080 1
 ```
