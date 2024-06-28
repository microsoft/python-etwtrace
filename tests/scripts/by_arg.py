import etwtrace
import sys

def a():
    etwtrace._mark_stack("A")
    if "b" in sys.argv:
        b()

def b():
    etwtrace._mark_stack("B")
    if "c" in sys.argv:
        c()

def c():
    etwtrace._mark_stack("C")


if "a" in sys.argv:
    a()
