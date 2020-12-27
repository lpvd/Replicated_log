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
replico root <rootaddress> <nodeaddress1;nodeaddress2> <threads>
  ```

### To start as a secondary node:
  ```
replico node <nodeadress> <rootaddress> <threads>
 ```
#### Example
 ```
/* Master */
 replico root 127.0.0.1:8080 127.0.0.1:8081;127.0.0.1:8082 1

/* Secondaries*/
replico node 127.0.0.1:8081 127.0.0.1:8080 1
replico node 127.0.0.1:8082 127.0.0.1:8080 1
 ```
