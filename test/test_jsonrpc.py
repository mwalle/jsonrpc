#!/usr/bin/env python

from __future__ import print_function
from collections import namedtuple
from subprocess import Popen, PIPE
import json
import nose
import sys
import os

def handle_request(request):
    def _json_object_hook(d):
        return namedtuple('X', d.keys())(*d.values())
    command = ['./handle_stdio']
    if 'USE_VALGRIND' in os.environ:
        command.insert(0, '--leak-check=full')
        command.insert(0, 'valgrind')
        print('running ' + ' '.join(command), file=sys.stderr)
        print(request, file=sys.stderr)
    serv = Popen(command, stdin=PIPE, stdout=PIPE)
    out, _ = serv.communicate(request)
    return json.loads(out, object_hook=_json_object_hook)

def test_parse_error():
    r = handle_request('''
            {asdf
    ''')
    assert r.jsonrpc == '2.0'
    assert not hasattr(r, 'result')
    assert r.error.code == -32700
    assert r.error.message == 'Parse error'
    assert r.id == None

def test_incorrect_jsonrpc():
    r = handle_request('''
            {
                "jsonrpc": "2.1",
                "method": "noop",
                "id": 1
            }
    ''')
    assert r.jsonrpc == '2.0'
    assert not hasattr(r, 'result')
    assert r.error.code == -32600
    assert r.error.message == 'Invalid Request'
    assert r.id == 1

def test_incorrect_id():
    r = handle_request('''
            {
                "jsonrpc": "2.0",
                "method": "noop",
                "id": {}
            }
    ''')
    assert r.jsonrpc == '2.0'
    assert not hasattr(r, 'result')
    assert r.error.code == -32600
    assert r.error.message == 'Invalid Request'
    assert r.id == None

def test_incorrect_params():
    r = handle_request('''
            {
                "jsonrpc": "2.0",
                "method": "noop",
                "params": 0,
                "id": 1
            }
    ''')
    assert r.jsonrpc == '2.0'
    assert not hasattr(r, 'result')
    assert r.error.code == -32600
    assert r.error.message == 'Invalid Request'
    assert r.id == 1

def test_unknown_method():
    r = handle_request('''
            {
                "jsonrpc": "2.0",
                "method": "unknown",
                "id": 1
            }
    ''')
    assert r.jsonrpc == '2.0'
    assert not hasattr(r, 'result')
    assert r.error.code == -32601
    assert r.error.message == 'Method not found'
    assert r.id == 1

def test_internal_error():
    r = handle_request('''
            {
                "jsonrpc": "2.0",
                "method": "internal_error",
                "id": 1
            }
    ''')
    assert r.jsonrpc == '2.0'
    assert not hasattr(r, 'result')
    assert r.error.code == -32603
    assert r.error.message == 'Internal error'
    assert r.id == 1

def test_invalid_params():
    r = handle_request('''
            {
                "jsonrpc": "2.0",
                "method": "invalid_params",
                "id": 1
            }
    ''')
    assert r.jsonrpc == '2.0'
    assert not hasattr(r, 'result')
    assert r.error.code == -32602
    assert r.error.message == 'Invalid params'
    assert r.id == 1

def test_basic_call():
    r = handle_request('''
            {
                "jsonrpc": "2.0",
                "method": "noop",
                "id": 1
            }
    ''')
    assert r.jsonrpc == '2.0'
    assert r.result == None
    assert not hasattr(r, 'error')
    assert r.id == 1

def test_add_valid_by_position():
    r = handle_request('''
            {
                "jsonrpc": "2.0",
                "method": "add",
                "params": [1, 2],
                "id": 1
            }
    ''')
    assert r.jsonrpc == '2.0'
    assert r.result == 3
    assert not hasattr(r, 'error')
    assert r.id == 1

def test_add_valid_by_name():
    r = handle_request('''
            {
                "jsonrpc": "2.0",
                "method": "add",
                "params": {"a": 1, "b": 2},
                "id": 1
            }
    ''')
    assert r.jsonrpc == '2.0'
    assert r.result == 3
    assert not hasattr(r, 'error')
    assert r.id == 1

def test_add_invalid_arguments():
    r = handle_request('''
            {
                "jsonrpc": "2.0",
                "method": "add",
                "params": {"a": "1", "b": 2},
                "id": 1
            }
    ''')
    assert r.jsonrpc == '2.0'
    assert not hasattr(r, 'result')
    assert r.error.code == -32602
    assert r.error.message == 'Invalid params'
    assert r.id == 1

def test_add_missing_argument():
    r = handle_request('''
            {
                "jsonrpc": "2.0",
                "method": "add",
                "params": {"a": "1"},
                "id": 1
            }
    ''')
    assert r.jsonrpc == '2.0'
    assert not hasattr(r, 'result')
    assert r.error.code == -32602
    assert r.error.message == 'Invalid params'
    assert r.id == 1

def test_id_integer():
    r = handle_request('''
            {
                "jsonrpc": "2.0",
                "method": "noop",
                "id": 1
            }
    ''')
    assert r.id == 1

def test_id_string():
    r = handle_request('''
            {
                "jsonrpc": "2.0",
                "method": "noop",
                "id": "abc"
            }
    ''')
    assert r.id == 'abc'

def test_id_null():
    r = handle_request('''
            {
                "jsonrpc": "2.0",
                "method": "noop",
                "id": null
            }
    ''')
    assert r.id == None

def test_id_number():
    r = handle_request('''
            {
                "jsonrpc": "2.0",
                "method": "noop",
                "id": 2.3
            }
    ''')
    assert r.id == 2.3

#def test_batch():
#    r = handle_request('''
#            [
#                {
#                    "jsonrpc": "2.0",
#                    "method": "noop",
#                    "id": 1
#                },
#                {
#                    "jsonrpc": "2.0",
#                    "method": "add",
#                    "params": [1, 2],
#                    "id": 2
#                }
#            ]
#    ''')
#
#    # this is only true for our implementation
#    assert not hasattr(r[0], 'error')
#    assert not hasattr(r[1], 'error')
#    assert r[0].id == 1
#    assert r[1].id == 2
#    assert r[1].result == 3
