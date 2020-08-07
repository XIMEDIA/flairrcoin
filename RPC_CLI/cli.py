import requests
import json
from os import system

url = "http://localhost:7076"

def acc_balances():
    print('account: ', end='')
    acc = [input()]
    data = {'action': 'accounts_balances', 'accounts': acc}
    headers = {'Content-type': 'application/json', 'Accept': 'text/plain'}
    r = requests.post(url, data=json.dumps(data), headers=headers)
    system('clear')
    print (json.dumps(r.json(),sort_keys=True, indent=4))

def wal_balances():
    print('wallet: ', end='')
    wal = input()
    data = {'action': 'wallet_balances', 'wallet': wal}
    headers = {'Content-type': 'application/json', 'Accept': 'text/plain'}
    r = requests.post(url, data=json.dumps(data), headers=headers)
    system('clear')
    print (json.dumps(r.json(),sort_keys=True, indent=4))

def send():
    print('source wallet: ', end='')
    wal = input()

    print('source acct. number: ', end='')
    src = input()

    print('destination acct. number: ', end='')
    dest = input()

    print('amount: ', end='')
    amnt = input()

    data = {'action': 'send', 'wallet': wal, 'source': src, 'destination':dest, 'amount':amnt}
    headers = {'Content-type': 'application/json', 'Accept': 'text/plain'}
    r = requests.post(url, data=json.dumps(data), headers=headers)
    system('clear')
    print (json.dumps(r.json(),sort_keys=True, indent=4))

def receive():
    print('wallet: ', end='')
    wal = input()

    print('account: ', end='')
    acc = input()

    print('block: ', end='')
    block = input()

    data = {'action': 'receive', 'wallet': wal, 'account': acc, 'block':block}
    headers = {'Content-type': 'application/json', 'Accept': 'text/plain'}
    r = requests.post(url, data=json.dumps(data), headers=headers)
    system('clear')
    print (json.dumps(r.json(),sort_keys=True, indent=4))

    data = {'action': 'wallet_balances', 'wallet': wal} #PRINT BALANCE AFTER RECIEVE
    r = requests.post(url, data=json.dumps(data), headers=headers)
    print (json.dumps(r.json(),sort_keys=True, indent=4))


def switch(selection):
    if (selection == 'a'):
        acc_balances()
    elif (selection == 's'):
        send()
    elif (selection == 'r'):
        receive()
    elif (selection == 'w'):
        wal_balances()
    


print('[s]end? [r]ecieve? [a]ccount balances? [w]allet balance?')
selection = input()

switch(selection)
    


# print('action: ', end='')
# action = input()
# print('arg1: ', end='')
# arg1 = input()
# print('arg2: ', end='')
# arg2 = input()

# data = {'action': action, arg1: arg2}
# headers = {'Content-type': 'application/json', 'Accept': 'text/plain'}
# r = requests.post(url, data=json.dumps(data), headers=headers)
# system('clear')
# print (r.json())