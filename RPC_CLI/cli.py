import requests
import json
from os import system

url = "http://localhost:7076"

# def acc_balances():
#     print('account: ', end='')
#     acc = input()
#     data = {'action': 'accounts_balances', 'accounts': acc}
#     headers = {'Content-type': 'application/json', 'Accept': 'text/plain'}
#     r = requests.post(url, data=json.dumps(data), headers=headers)
#     system('clear')
#     print (r.json())


# def switch(selection):
#     if (selection == 'a'):
#         acc_balances()

# print('[s]end? [r]ecieve? [a]ccount balances? [w]allet balance? [c]reate account? wa[l]let create?')
# selection = input()

# switch(selection)
    


print('action: ', end='')
action = input()
print('arg1: ', end='')
arg1 = input()
print('arg2: ', end='')
arg2 = input()

data = {'action': action, arg1: arg2}
headers = {'Content-type': 'application/json', 'Accept': 'text/plain'}
r = requests.post(url, data=json.dumps(data), headers=headers)
system('clear')
print (r.json())