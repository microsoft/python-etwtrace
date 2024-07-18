import etwtrace
import sys
import threading

def a(x):
    if x:
        b(x - 1)
    else:
        etwtrace._mark_stack("Test")

def b(x):
    if x:
        c(x - 1)
    else:
        etwtrace._mark_stack("Test")

def c(x):
    x += 1
    etwtrace._mark_stack("Test")


if __name__ == "__main__":
    threads = [threading.Thread(target=a, args=(i,)) for i in range(10)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
