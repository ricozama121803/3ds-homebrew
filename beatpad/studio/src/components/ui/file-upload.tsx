"use client";
// Aceternity UI "File Upload" (ui.aceternity.com), adapted for 3DS Theme Studio (reused in BeatPad Song Studio):
// accept filters, custom labels, a compact size, unique input ids, no hover
// animation, and the parent owns the file (no internal file list).
import { cn } from "@/lib/utils";
import React, { useId, useRef } from "react";
import { motion } from "motion/react";
import { IconUpload } from "@tabler/icons-react";
import { useDropzone, type Accept } from "react-dropzone";

export const FileUpload = ({
  onChange,
  accept,
  title = "Upload file",
  subtitle = "Drag or drop your file here or click to upload",
  compact = false,
  className,
}: {
  onChange?: (files: File[]) => void;
  accept?: Accept;
  title?: string;
  subtitle?: string;
  compact?: boolean;
  className?: string;
}) => {
  const fileInputRef = useRef<HTMLInputElement>(null);
  const inputId = useId();

  const handleFileChange = (newFiles: File[]) => {
    if (newFiles.length) onChange?.(newFiles);
  };

  const { getRootProps, isDragActive } = useDropzone({
    multiple: false,
    noClick: true,
    accept,
    onDrop: handleFileChange,
  });

  const acceptAttr = accept ? Object.keys(accept).join(",") : undefined;

  return (
    <div className={cn("w-full", className)} {...getRootProps()}>
      <motion.div
        onClick={() => fileInputRef.current?.click()}
        className={cn(
          "relative block w-full cursor-pointer overflow-hidden rounded-lg border border-neutral-800 transition-colors hover:border-neutral-600",
          compact ? "p-4" : "p-8",
        )}
      >
        <input
          ref={fileInputRef}
          id={inputId}
          type="file"
          accept={acceptAttr}
          onChange={(e) => {
            handleFileChange(Array.from(e.target.files || []));
            e.target.value = "";
          }}
          className="hidden"
        />
        <div className="absolute inset-0 [mask-image:radial-gradient(ellipse_at_center,white,transparent)]">
          <GridPattern />
        </div>
        <div className="flex flex-col items-center justify-center text-center">
          <p className={cn("relative z-20 font-sans font-bold text-neutral-300", compact ? "text-sm" : "text-base")}>
            {title}
          </p>
          {!compact && (
            <p className="relative z-20 mt-2 font-sans text-sm font-normal text-neutral-400">{subtitle}</p>
          )}
          <div className={cn("relative mx-auto w-full", compact ? "mt-3 max-w-[4rem]" : "mt-6 max-w-[8rem]")}>
            <motion.div
              className={cn(
                "relative z-40 mx-auto flex w-full items-center justify-center rounded-md bg-neutral-900",
                "shadow-[0px_10px_50px_rgba(0,0,0,0.1)]",
                compact ? "h-14" : "h-28",
              )}
            >
              {isDragActive ? (
                <motion.p
                  initial={{ opacity: 0 }}
                  animate={{ opacity: 1 }}
                  className="flex flex-col items-center text-xs text-neutral-300"
                >
                  Drop it
                  <IconUpload className="h-4 w-4 text-neutral-400" />
                </motion.p>
              ) : (
                <IconUpload className="h-4 w-4 text-neutral-300" />
              )}
            </motion.div>
          </div>
        </div>
      </motion.div>
    </div>
  );
}

export function GridPattern() {
  const columns = 41;
  const rows = 11;
  return (
    <div className="flex shrink-0 scale-105 flex-wrap items-center justify-center gap-x-px gap-y-px bg-neutral-900">
      {Array.from({ length: rows }).map((_, row) =>
        Array.from({ length: columns }).map((_, col) => {
          const index = row * columns + col;
          return (
            <div
              key={`${col}-${row}`}
              className={`flex h-10 w-10 shrink-0 rounded-[2px] ${
                index % 2 === 0
                  ? "bg-neutral-950"
                  : "bg-neutral-950 shadow-[0px_0px_1px_3px_rgba(0,0,0,1)_inset]"
              }`}
            />
          );
        }),
      )}
    </div>
  );
}
