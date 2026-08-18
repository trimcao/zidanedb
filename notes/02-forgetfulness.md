# The First Problem: Forgetfulness

## Motivation

We have our stupidly simple kv store. It runs, it can put keys, get keys, delete keys. Great. But when `zidane` stops running, he does not remember anything. Databases must remember stuffs. `zidane` should remember his headbutt against `matrix`. Forgetfulness is not good for `zidane`.


## My Thoughts on a Solution

Let's start with a very naive implementation, and go from there.
But the first question is, what is the format of the db file?
Sadly, I am already stumped here. Problems are:
- If keys and values can be multi-line, then it will be complicated.
- If I choose to do: <key>=<value>, then what happens if either key or value has
the character `=`?

I guess, I will implement a very rough draft first. And there will be many problems
that I don't even see right now. But it's ok.
My current assumptions are:
- Keys and values are one-line only. No new-line character allowed.
- The format of the database file will be roughly:
```
<key-0>
<value-0>
<key-1>
<value-1>
...
```
- I mean, new-line character is probably ok as long as we can save all the special
characters into the same line. For example, new-line will be stored as `\n` in the string.
So a string like this `line1\nline2\line3` can be 3 lines long, but on the db file, we
can still process it as one line. That's the idea, but I don't know exactly how
to do it.
- Follow up the point above, is that one of the reasons we want to use binary format?
Maybe not binary (for now), but `base64` definitely sounds promising.
- For now, let's just follow this plan, then I can deal with the problems later.
Most likely I will need to change the file format anyway :).


Next question: when do I open the db file?
- Naive: just open the file whenever I do `put` or `delete`.


One note from Codex:
- A safer persistence design will eventually write to a temporary file first and replace the original only after the complete write succeeds.

## Challenges from Matrix


## What's Next


## Q&A