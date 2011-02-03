/*!
 * \file cf-parse.y
 *
 * \author Ondrej Sury <ondrej.sury@nic.cz>
 *
 * \brief Server configuration structures and API.
 */
%{
/* Headers */
#include <stdio.h>
#include <string.h>
#include "conf.h"

extern void cf_error(const char *msg);
extern config_t *new_config;
static conf_iface_t *this_iface;
static conf_key_t *this_key;
static conf_server_t *this_server;
static conf_zone_t *this_zone;

%}

%union {
    char *t;
    int i;
    tsig_alg_t alg;
}

%token END INVALID_TOKEN
%token SYSTEM IDENTITY VERSION LOG
%token INTERFACES ADDRESS PORT
%token ALGORITHM SECRET
%token SERVERS KEYS KEY INTERFACE
%token ZONES STORAGE
%token <t> ZONE
%token <t> TEXT
%token <t> IPA

%token <i> NUM
%token <alg> TSIG_ALGO_NAME

%%

config: conf_entries END { return 0; } ;

conf_entries:
 /* EMPTY */
 | conf_entries conf
 ;

interface_start: TEXT {
    this_iface = malloc(sizeof(conf_iface_t));
    memset(this_iface, 0, sizeof(conf_iface_t));
    this_iface->name = $1;
    this_iface->address = 0; // No default address (mandatory)
    this_iface->port = CONFIG_DEFAULT_PORT;
    add_tail(&new_config->ifaces, &this_iface->n);
 }
 ;

interface:
   interface_start '{'
 | interface ADDRESS IPA ';' { this_iface->address = $3; }
 | interface PORT NUM ';' { this_iface->port = $3; }
 ;

interfaces:
   INTERFACES '{'
 | interfaces interface '}'
 ;

key_start: TEXT {
  this_key = malloc(sizeof(conf_key_t));
  memset(this_key, 0, sizeof(conf_key_t));
  this_key->name = $1;
  add_tail(&new_config->keys, &this_key->n);
 }
 ;

key:
   key_start '{'
 | key ALGORITHM TSIG_ALGO_NAME ';' { this_key->algorithm = $3; }
 | key SECRET TEXT ';' { this_key->secret = $3; }
 ;

keys:
   KEYS '{'
 | keys key '}'
 ;

server_start: TEXT {
  this_server = malloc(sizeof(conf_server_t));
  memset(this_server, 0, sizeof(conf_server_t));
  this_server->name = $1;
  add_tail(&new_config->servers, &this_server->n);
 }
 ;

server:
   server_start '{'
 | server ADDRESS IPA ';' { this_server->address = $3; }
 | server PORT NUM ';' { this_server->port = $3; }
 | server KEY TEXT ';' {
	 this_server->key = malloc(sizeof(conf_key_t));
	 this_server->key->name = $3;

   }
 | server INTERFACE TEXT ';' {
	 this_server->iface = malloc(sizeof(conf_iface_t));
	 this_server->iface->name = $3;
   }
 ;

servers:
   SERVERS '{'
 | servers server '}'
 ;

system:
   SYSTEM '{'
 | system VERSION TEXT ';' { new_config->version = $3; }
 | system IDENTITY TEXT ';' { new_config->identity = $3; }
 ;

zones:
   ZONES '{'
 | zones zone '}'
 ;

zone_start: ZONE {
    this_zone = malloc(sizeof(conf_zone_t));
    memset(this_zone, 0, sizeof(conf_zone_t));
    this_zone->name = $1;
    add_tail(&new_config->zones, &this_zone->n);
 }
 ;

zone:
   zone_start '{'
 | zone STORAGE TEXT ';' { this_zone->storage = $3; }
 ;


conf: ';' | system '}' | interfaces '}' | keys '}' | servers '}' | zones '}';

%%
