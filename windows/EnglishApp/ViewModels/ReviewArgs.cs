using EnglishApp.Native;

namespace EnglishApp.ViewModels;

public sealed record ReviewArgs(string TextId, string Reference, string Json, AssessmentResult Result, string? Note);
