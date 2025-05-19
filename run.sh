#!/bin/bash

mvn clean
mvn compile

TARGET_DIR="target/classes"
PREFIX="linux-x64"
LIB_FILE="libsylvan-java.so"
SOURCE_PATH="lib/$LIB_FILE"  # Change this to the actual source path

if [ ! -d "$TARGET_DIR/$PREFIX" ]; then
    mkdir -p "$TARGET_DIR/$PREFIX"
    echo "Directory $PREFIX created."
else
    echo "Directory $PREFIX already exists."
fi

if [ ! -f "$TARGET_DIR/$PREFIX/$LIB_FILE" ]; then
    cp "$SOURCE_PATH" "$TARGET_DIR/$PREFIX/"
    echo "$LIB_FILE copied to $TARGET_DIR/$PREFIX."
else
    echo "$LIB_FILE already exists in $TARGET_DIR/$PREFIX."
fi

# run NQueens-NDD