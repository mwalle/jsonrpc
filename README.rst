A small JSON-RPC server implementation in C
===========================================

This little project aims to be a simple but fully compliant JSON-RPC server
implementation. It uses Jansson_ to parse the JSON messages.

It provides the following features:

 * Decoding of the request and encoding of the response
 * Message dispatching
 * Simple API
 * Error handling
 * Simple text-based test suite

What's not included:

 * A transport layer. JSON-RPC does not specify a transport layer. You have
   to build your own. For example the test suite uses pipes for input and
   output.

.. _Jansson: http://www.digip.org/jansson/
