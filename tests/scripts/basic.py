import etwtrace

def a():
    b()

def b():
    etwtrace._mark_stack("Test")

a()
