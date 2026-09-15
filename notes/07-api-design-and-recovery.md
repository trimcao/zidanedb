# API Design and Recovery

## API Design
The basic question: what do PUT, GET, and DELETE actually mean?

PUT scenarios:
- PUT with empty value
- PUT overwrite

GET scenarios:
- GET a key with an empty value
- GET a non-existent key

DELETE scenarios:
- DELETE a non-existent key

What I want right now:
- PUT with empty value should be allowed.
- DELETE a non-existent key should notify the users, maybe an error is ok.

Current problems:
- Currently, the db file will use empty value as an indicator of a deleted
key. What should I do?
- If I want to do PUT with empty value, how to handle empty value
in the `zidane` command line?

Solutions:
- Having a type of db entry will help us differentiate between a deleted
key and an existing key with an empty value.
- CLI11 should handle empty value just fine. Just need to test it
thoroughly.

## Recovery