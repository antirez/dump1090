bytelen = 8

# Numbers where there is a pair of 1 is all numbers comprised of a 3 and then doubled. i.g. 3 6 12 24
A = ["0","0","0","0","0","0","0","0"]
def binary(n):
    for i in range(8):
        B = A[:]
        B[i] = '1'
        str = "0b"
        for letter in B:
            str += letter
        str += ","
        print(str)

binary(7)
